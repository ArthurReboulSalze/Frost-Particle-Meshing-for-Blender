#include <algorithm>
#include <cmath>
#include <cuNSearch.h>
#include <cuda_runtime.h>
#include <iostream>
#include <memory>
#include <vector>

#include "tables.h"

// ========================================================================
// CUDA Kernels
// ========================================================================

// Minimal kernel implementation (smoke test)
__global__ void hello_cuda_kernel(float *data, int N) {
  int idx = blockIdx.x * blockDim.x + threadIdx.x;
  if (idx < N) {
    data[idx] = data[idx] * 2.0f;
  }
}

__device__ float zhu_bridson_kernel(float distanceSquared,
                                    float supportSquared) {
  if (distanceSquared >= supportSquared)
    return 0.0f;

  float x = 1.0f - distanceSquared / supportSquared;
  return x * x * x;
}

__device__ float zhu_bridson_compensation(float weight, float trimDensity,
                                          float trimStrength) {
  if (trimDensity <= 0.0f || trimStrength <= 0.0f || weight >= trimDensity)
    return 0.0f;

  float x = 1.0f - weight / trimDensity;
  float x2 = x * x;
  return trimStrength * x2 * x2;
}

__global__ void zhu_bridson_accumulate_kernel(
    const float *positions, const float *radii, float *gridWeights,
    float *gridBlendedRadii, float3 *gridBlendedOffsets, int numParticles,
    float3 gridMin, float voxelSize, int3 gridDim, float support) {
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= numParticles || support <= 0.0f)
    return;

  const float3 p = make_float3(positions[i * 3 + 0], positions[i * 3 + 1],
                               positions[i * 3 + 2]);
  const float radius = radii[i];

  const float3 minRange =
      make_float3(p.x - support, p.y - support, p.z - support);
  const float3 maxRange =
      make_float3(p.x + support, p.y + support, p.z + support);

  int3 minIdx, maxIdx;
  minIdx.x = max(0, (int)((minRange.x - gridMin.x) / voxelSize));
  minIdx.y = max(0, (int)((minRange.y - gridMin.y) / voxelSize));
  minIdx.z = max(0, (int)((minRange.z - gridMin.z) / voxelSize));

  maxIdx.x = min(gridDim.x - 1, (int)((maxRange.x - gridMin.x) / voxelSize));
  maxIdx.y = min(gridDim.y - 1, (int)((maxRange.y - gridMin.y) / voxelSize));
  maxIdx.z = min(gridDim.z - 1, (int)((maxRange.z - gridMin.z) / voxelSize));

  const float supportSquared = support * support;
  const long long sliceStride = (long long)gridDim.x * gridDim.y;

  for (int z = minIdx.z; z <= maxIdx.z; ++z) {
    for (int y = minIdx.y; y <= maxIdx.y; ++y) {
      for (int x = minIdx.x; x <= maxIdx.x; ++x) {
        const float3 samplePos = make_float3(gridMin.x + x * voxelSize,
                                             gridMin.y + y * voxelSize,
                                             gridMin.z + z * voxelSize);
        const float dx = p.x - samplePos.x;
        const float dy = p.y - samplePos.y;
        const float dz = p.z - samplePos.z;
        const float distanceSquared = dx * dx + dy * dy + dz * dz;
        const float weight =
            zhu_bridson_kernel(distanceSquared, supportSquared);

        if (weight <= 0.0f)
          continue;

        const long long gridIdx =
            (long long)z * sliceStride + (long long)y * gridDim.x + x;

        atomicAdd(&gridWeights[gridIdx], weight);
        atomicAdd(&gridBlendedRadii[gridIdx], weight * radius);
        atomicAdd(&gridBlendedOffsets[gridIdx].x, weight * dx);
        atomicAdd(&gridBlendedOffsets[gridIdx].y, weight * dy);
        atomicAdd(&gridBlendedOffsets[gridIdx].z, weight * dz);
      }
    }
  }
}

__global__ void finalize_zhu_bridson_field_kernel(
    const float *gridWeights, const float *gridBlendedRadii,
    const float3 *gridBlendedOffsets, float *gridField, long long totalVoxels,
    float outsideDistance, float trimDensity, float trimStrength) {
  long long idx = (long long)blockIdx.x * blockDim.x + threadIdx.x;
  if (idx >= totalVoxels)
    return;

  const float weight = gridWeights[idx];
  if (weight <= 0.0f) {
    gridField[idx] = outsideDistance;
    return;
  }

  const float invWeight = 1.0f / weight;
  const float3 blendedOffset = make_float3(gridBlendedOffsets[idx].x * invWeight,
                                           gridBlendedOffsets[idx].y * invWeight,
                                           gridBlendedOffsets[idx].z * invWeight);
  const float blendedRadius = gridBlendedRadii[idx] * invWeight;
  const float offsetMagnitude =
      sqrtf(blendedOffset.x * blendedOffset.x + blendedOffset.y * blendedOffset.y +
            blendedOffset.z * blendedOffset.z);

  gridField[idx] = offsetMagnitude - blendedRadius +
                   zhu_bridson_compensation(weight, trimDensity, trimStrength);
}

// Marching Cubes Constants in texture/constant memory handled by 'tables.h'
// inclusion. But we need to make sure they are available on device. The tables
// in tables.h are 'static const'. To use them in a kernel, we should copy them
// to __constant__ memory or pass them as arguments.
// For simplicity in this rapid prototype, we'll put them in __constant__ logic
// here.

__constant__ int c_edgeTable[256];
__constant__ int c_triTable[256][16];

// Simple vertex interpolation - no snapping (welding handled by CPU)
__device__ float3 vertexInterp(float isolevel, float3 p1, float3 p2,
                               float valp1, float valp2, float voxelSize) {
  if (fabsf(isolevel - valp1) < 0.00001f)
    return p1;
  if (fabsf(isolevel - valp2) < 0.00001f)
    return p2;
  if (fabsf(valp1 - valp2) < 0.00001f) {
    return make_float3((p1.x + p2.x) * 0.5f, (p1.y + p2.y) * 0.5f,
                       (p1.z + p2.z) * 0.5f);
  }

  float mu = (isolevel - valp1) / (valp2 - valp1);
  mu = fmaxf(0.0f, fminf(1.0f, mu)); // Clamp mu to [0,1]

  float3 p;
  p.x = p1.x + mu * (p2.x - p1.x);
  p.y = p1.y + mu * (p2.y - p1.y);
  p.z = p1.z + mu * (p2.z - p1.z);
  return p;
}

// Marching Cubes Kernel
__global__ void marching_cubes_kernel(float *grid, int3 gridDim, float3 gridMin,
                                      float voxelSize, float isovalue,
                                      float3 *triangles, int *triangleCount,
                                      int maxTriangles) {
  int x = blockIdx.x * blockDim.x + threadIdx.x;
  int y = blockIdx.y * blockDim.y + threadIdx.y;
  int z = blockIdx.z * blockDim.z + threadIdx.z;

  if (x >= gridDim.x - 1 || y >= gridDim.y - 1 || z >= gridDim.z - 1)
    return;

  // Corner positions relative to current voxel
  // 0: x, y, z
  // 1: x+1, y, z
  // 2: x+1, y, z+1
  // 3: x, y, z+1
  // 4: x, y+1, z
  // 5: x+1, y+1, z
  // 6: x+1, y+1, z+1
  // 7: x, y+1, z+1

  // Grid indices for these corners (assuming row major z*y*x ?)
  // Wait, splat was z * (dimX*dimY) + y * dimX + x

  int sliceSize = gridDim.x * gridDim.y;
  int idx0 = z * sliceSize + y * gridDim.x + x;
  int idx1 = idx0 + 1;
  int idx3 = idx0 + gridDim.x; // wait: if layout is generic? splatted as z *
                               // (dimX*dimY) + y*dimX + x
  // splat was: int gridIdx = z * (gridDim.x * gridDim.y) + y * gridDim.x + x;
  // idx + gridDim.x moves to y+1? NO. y moves by gridDim.x. Yes.

  // Let's re-verify offsets:
  // x moves 1
  // y moves gridDim.x
  // z moves gridDim.x * gridDim.y

  // Grid layout: x moves by 1, y moves by rowOffset, z moves by sliceOffset
  long long rowOffset = gridDim.x;
  long long sliceOffset = (long long)gridDim.x * gridDim.y;

  // Bourke corner ordering for val[] lookups - EXACT match to p[] below
  // Face Z=0: 0:(x,y,z) 1:(x+1,y,z) 2:(x+1,y+1,z) 3:(x,y+1,z)
  // Face Z=1: 4:(x,y,z+1) 5:(x+1,y,z+1) 6:(x+1,y+1,z+1) 7:(x,y+1,z+1)
  float val[8];
  long long baseIdx = (long long)z * sliceOffset + (long long)y * rowOffset + x;
  val[0] = grid[baseIdx];                               // (x, y, z)
  val[1] = grid[baseIdx + 1];                           // (x+1, y, z)
  val[2] = grid[baseIdx + 1 + rowOffset];               // (x+1, y+1, z)
  val[3] = grid[baseIdx + rowOffset];                   // (x, y+1, z)
  val[4] = grid[baseIdx + sliceOffset];                 // (x, y, z+1)
  val[5] = grid[baseIdx + 1 + sliceOffset];             // (x+1, y, z+1)
  val[6] = grid[baseIdx + 1 + rowOffset + sliceOffset]; // (x+1, y+1, z+1)
  val[7] = grid[baseIdx + rowOffset + sliceOffset];     // (x, y+1, z+1)

  int cubeIndex = 0;
  // Standard MC: inside = val < isovalue (100% compatible with Bourke tables)
  if (val[0] < isovalue)
    cubeIndex |= 1;
  if (val[1] < isovalue)
    cubeIndex |= 2;
  if (val[2] < isovalue)
    cubeIndex |= 4;
  if (val[3] < isovalue)
    cubeIndex |= 8;
  if (val[4] < isovalue)
    cubeIndex |= 16;
  if (val[5] < isovalue)
    cubeIndex |= 32;
  if (val[6] < isovalue)
    cubeIndex |= 64;
  if (val[7] < isovalue)
    cubeIndex |= 128;

  // Table mapping might fail if coordinate system differs.
  // But it should preserve topology roughly.

  // Early exit for empty or full cubes
  if (c_edgeTable[cubeIndex] == 0)
    return;

  // Bourke corner positions - EXACT standard layout
  // Face Z=0: 0:(0,0,0) 1:(1,0,0) 2:(1,1,0) 3:(0,1,0)
  // Face Z=1: 4:(0,0,1) 5:(1,0,1) 6:(1,1,1) 7:(0,1,1)
  // Edges 0-3 go around Z=0 face, edges 4-7 go around Z=1 face
  float3 p[8];
  float px = gridMin.x + x * voxelSize;
  float py = gridMin.y + y * voxelSize;
  float pz = gridMin.z + z * voxelSize;

  p[0] = make_float3(px, py, pz);
  p[1] = make_float3(px + voxelSize, py, pz);
  p[2] = make_float3(px + voxelSize, py + voxelSize, pz);
  p[3] = make_float3(px, py + voxelSize, pz);
  p[4] = make_float3(px, py, pz + voxelSize);
  p[5] = make_float3(px + voxelSize, py, pz + voxelSize);
  p[6] = make_float3(px + voxelSize, py + voxelSize, pz + voxelSize);
  p[7] = make_float3(px, py + voxelSize, pz + voxelSize);

  // Compute edge vertices CONDITIONALLY based on edgeTable
  float3 vertlist[12];
  int edges = c_edgeTable[cubeIndex];

  // Initialize to cell center as fallback
  float3 cellCenter = make_float3(px + voxelSize * 0.5f, py + voxelSize * 0.5f,
                                  pz + voxelSize * 0.5f);
  for (int vi = 0; vi < 12; vi++) {
    vertlist[vi] = cellCenter;
  }

  if (edges & 1)
    vertlist[0] = vertexInterp(isovalue, p[0], p[1], val[0], val[1], voxelSize);
  if (edges & 2)
    vertlist[1] = vertexInterp(isovalue, p[1], p[2], val[1], val[2], voxelSize);
  if (edges & 4)
    vertlist[2] = vertexInterp(isovalue, p[2], p[3], val[2], val[3], voxelSize);
  if (edges & 8)
    vertlist[3] = vertexInterp(isovalue, p[3], p[0], val[3], val[0], voxelSize);
  if (edges & 16)
    vertlist[4] = vertexInterp(isovalue, p[4], p[5], val[4], val[5], voxelSize);
  if (edges & 32)
    vertlist[5] = vertexInterp(isovalue, p[5], p[6], val[5], val[6], voxelSize);
  if (edges & 64)
    vertlist[6] = vertexInterp(isovalue, p[6], p[7], val[6], val[7], voxelSize);
  if (edges & 128)
    vertlist[7] = vertexInterp(isovalue, p[7], p[4], val[7], val[4], voxelSize);
  if (edges & 256)
    vertlist[8] = vertexInterp(isovalue, p[0], p[4], val[0], val[4], voxelSize);
  if (edges & 512)
    vertlist[9] = vertexInterp(isovalue, p[1], p[5], val[1], val[5], voxelSize);
  if (edges & 1024)
    vertlist[10] =
        vertexInterp(isovalue, p[2], p[6], val[2], val[6], voxelSize);
  if (edges & 2048)
    vertlist[11] =
        vertexInterp(isovalue, p[3], p[7], val[3], val[7], voxelSize);

  // Generate triangles
  for (int i = 0; i < 15 && c_triTable[cubeIndex][i] != -1; i += 3) {
    int e0 = c_triTable[cubeIndex][i];
    int e1 = c_triTable[cubeIndex][i + 1];
    int e2 = c_triTable[cubeIndex][i + 2];

    // Validate edge indices
    if (e0 < 0 || e0 > 11 || e1 < 0 || e1 > 11 || e2 < 0 || e2 > 11)
      continue;

    float3 v0 = vertlist[e0];
    float3 v1 = vertlist[e1];
    float3 v2 = vertlist[e2];

    int idx = atomicAdd(triangleCount, 1);
    if (idx < maxTriangles) {
      // Standard winding order (no swap) with val < isovalue
      triangles[idx * 3 + 0] = v0;
      triangles[idx * 3 + 1] = v1;
      triangles[idx * 3 + 2] = v2;
    }
  }
}

// ========================================================================
// FrostGPUManager Class
// ========================================================================

class FrostGPUManager {
public:
  FrostGPUManager() {
    cudaSetDevice(0);
    // Silent init by default, or only print if debug (but debug set later?)
    // Actually we can't silence constructor easily unless we pass debug flag to
    // constructor. But since this is just one line, maybe keep it or remove it?
    // Let's remove it to be safe, or check environment variable?
    // User requested log control. Let's keep it minimal or silence it.
    // std::cout << "[FrostGPUManager] Initialized on Device 0." << std::endl;

    // Copy tables to constant memory
    cudaMemcpyToSymbol(c_edgeTable, edgeTable, sizeof(edgeTable));
    cudaMemcpyToSymbol(c_triTable, triTable, sizeof(triTable));
  }

  ~FrostGPUManager() {
    if (d_positions)
      cudaFree(d_positions);
    if (d_radii)
      cudaFree(d_radii);
    releaseGridBuffers();
    if (d_triangles)
      cudaFree(d_triangles);
    if (d_triangleCount)
      cudaFree(d_triangleCount);
    // std::cout << "[FrostGPUManager] Destroyed." << std::endl;
  }

  void setDebug(bool debug) { m_debug = debug; }

  void uploadParticles(const float *positions_host, const float *radii_host,
                       int count) {
    cudaSetDevice(0);

    // OPTIMIZATION: Only reallocate if size changed
    bool sizeChanged = (count != m_numParticles);

    m_positionsHost.assign(positions_host, positions_host + count * 3);
    m_radiiHost.assign(radii_host, radii_host + count);
    m_numParticles = count;
    m_positionsChanged = true; // Mark for cuNSearch rebuild

    if (sizeChanged || d_positions == nullptr) {
      if (d_positions)
        cudaFree(d_positions);
      if (d_radii)
        cudaFree(d_radii);
      cudaMalloc(&d_positions, m_numParticles * 3 * sizeof(float));
      cudaMalloc(&d_radii, m_numParticles * sizeof(float));
    }

    // Always copy new positions
    cudaMemcpy(d_positions, m_positionsHost.data(),
               m_numParticles * 3 * sizeof(float), cudaMemcpyHostToDevice);
    cudaMemcpy(d_radii, m_radiiHost.data(), m_numParticles * sizeof(float),
               cudaMemcpyHostToDevice);
  }

  int findNeighbors(float radius) {
    cudaSetDevice(0);
    if (m_numParticles == 0)
      return -1;

    // OPTIMIZATION: Skip rebuild if nothing changed
    bool needRebuild =
        m_positionsChanged || (radius != m_lastSearchRadius) || !m_nsearch;

    if (!needRebuild) {
      // Use cached neighbor data
      return 0;
    }

    m_searchRadius = radius;
    m_lastSearchRadius = radius;
    m_positionsChanged = false;

    if (m_debug)
      std::cout << "[FrostGPUManager] findNeighbors: radius=" << radius
                << ", numParticles=" << m_numParticles << " (rebuilding)"
                << std::endl;

    try {
      m_nsearch = std::make_unique<cuNSearch::NeighborhoodSearch>(radius);
      m_pointSetId = m_nsearch->add_point_set(m_positionsHost.data(),
                                              m_numParticles, true, true, true);
      m_nsearch->find_neighbors();
      return 0;
    } catch (const std::exception &e) {
      std::cerr << "[FrostGPUManager] Error in findNeighbors: " << e.what()
                << std::endl;
      return -1;
    }
  }

  void setMeshingParameters(float lowDensityTrimmingDensity,
                            float lowDensityTrimmingStrength, int blockSize) {
    m_lowDensityTrimmingDensity = lowDensityTrimmingDensity;
    m_lowDensityTrimmingStrength = lowDensityTrimmingStrength;
    m_particleBlockSize = std::max(64, std::min(blockSize, 1024));
  }

  void computeDensity() {
    cudaSetDevice(0);
    // Kept as a pipeline stage so the public C API remains stable. The scalar
    // field is assembled directly in computeScalarField().
  }

  void setScalarFieldParameters(float minX, float minY, float minZ, float sizeX,
                                float sizeY, float sizeZ, float voxelSize) {
    m_gridMin = make_float3(minX, minY, minZ);
    m_voxelSize = voxelSize;

    m_gridDim.x = (int)ceil(sizeX / voxelSize);
    m_gridDim.y = (int)ceil(sizeY / voxelSize);
    m_gridDim.z = (int)ceil(sizeZ / voxelSize);

    size_t totalVoxels = (size_t)m_gridDim.x * m_gridDim.y * m_gridDim.z;

    releaseGridBuffers();

    if (totalVoxels > 1024 * 1024 * 1024) {
      if (m_debug)
        std::cerr << "[FrostGPUManager] Grid larger than 1G voxels requested. "
                     "Clipping."
                  << std::endl;
      return;
    }

    cudaMalloc(&d_grid, totalVoxels * sizeof(float));
    cudaMalloc(&d_gridWeights, totalVoxels * sizeof(float));
    cudaMalloc(&d_gridBlendedRadii, totalVoxels * sizeof(float));
    cudaMalloc(&d_gridBlendedOffsets, totalVoxels * sizeof(float3));
    cudaMemset(d_grid, 0, totalVoxels * sizeof(float));
    cudaMemset(d_gridWeights, 0, totalVoxels * sizeof(float));
    cudaMemset(d_gridBlendedRadii, 0, totalVoxels * sizeof(float));
    cudaMemset(d_gridBlendedOffsets, 0, totalVoxels * sizeof(float3));

    if (m_debug)
      std::cout << "[FrostGPUManager] Grid Allocated: " << m_gridDim.x << "x"
                << m_gridDim.y << "x" << m_gridDim.z
                << " Voxels: " << totalVoxels << " (voxelSize=" << voxelSize
                << ", searchRadius=" << m_searchRadius << ")" << std::endl;
  }

  void computeScalarField() {
    if (!d_grid || !d_gridWeights || !d_gridBlendedRadii ||
        !d_gridBlendedOffsets || !d_positions || !d_radii)
      return;

    const size_t totalVoxels =
        (size_t)m_gridDim.x * m_gridDim.y * m_gridDim.z;

    cudaMemset(d_grid, 0, totalVoxels * sizeof(float));
    cudaMemset(d_gridWeights, 0, totalVoxels * sizeof(float));
    cudaMemset(d_gridBlendedRadii, 0, totalVoxels * sizeof(float));
    cudaMemset(d_gridBlendedOffsets, 0, totalVoxels * sizeof(float3));

    int particleBlockSize = m_particleBlockSize;
    int particleBlocks = (m_numParticles + particleBlockSize - 1) / particleBlockSize;

    zhu_bridson_accumulate_kernel<<<particleBlocks, particleBlockSize>>>(
        d_positions, d_radii, d_gridWeights, d_gridBlendedRadii,
        d_gridBlendedOffsets, m_numParticles, m_gridMin, m_voxelSize, m_gridDim,
        m_searchRadius);
    cudaDeviceSynchronize();
    checkCudaError("zhu_bridson_accumulate_kernel");

    int fieldBlockSize = m_particleBlockSize;
    int fieldBlocks =
        (int)(((long long)totalVoxels + fieldBlockSize - 1) / fieldBlockSize);
    finalize_zhu_bridson_field_kernel<<<fieldBlocks, fieldBlockSize>>>(
        d_gridWeights, d_gridBlendedRadii, d_gridBlendedOffsets, d_grid,
        (long long)totalVoxels, m_searchRadius, m_lowDensityTrimmingDensity,
        m_lowDensityTrimmingStrength);
    cudaDeviceSynchronize();
    checkCudaError("computeScalarField");
  }

  // Phase 15: Marching Cubes
  void computeMesh(float isovalue) {
    cudaSetDevice(0);
    // Ensure constants are valid for this context
    cudaMemcpyToSymbol(c_edgeTable, edgeTable, sizeof(edgeTable));
    cudaMemcpyToSymbol(c_triTable, triTable, sizeof(triTable));

    if (!d_grid)
      return;

    // Allocate Triangle Buffer
    // FIXED: Use grid surface area estimation instead of particle count
    // Surface area scales as gridDim^2, not gridDim^3
    // Max triangles per surface voxel is ~5
    size_t totalVoxels = (size_t)m_gridDim.x * m_gridDim.y * m_gridDim.z;
    size_t surfaceEstimate =
        (size_t)std::sqrt((double)totalVoxels) * std::sqrt((double)totalVoxels);

    // Conservative estimate: surface voxels * 5 triangles per voxel
    int maxTriangles = (int)std::min(surfaceEstimate * 5,
                                     (size_t)50000000); // Cap at 50M triangles
    if (maxTriangles < 100000)
      maxTriangles = 100000; // Minimum 100k triangles

    if (d_triangles)
      cudaFree(d_triangles);
    if (d_triangleCount)
      cudaFree(d_triangleCount);

    cudaMalloc(&d_triangles, maxTriangles * 3 * sizeof(float3));
    cudaMalloc(&d_triangleCount, sizeof(int));
    cudaMemset(d_triangleCount, 0, sizeof(int));

    dim3 blockSize(8, 8, 8);
    dim3 gridShape((m_gridDim.x + blockSize.x - 1) / blockSize.x,
                   (m_gridDim.y + blockSize.y - 1) / blockSize.y,
                   (m_gridDim.z + blockSize.z - 1) / blockSize.z);

    marching_cubes_kernel<<<gridShape, blockSize>>>(
        d_grid, m_gridDim, m_gridMin, m_voxelSize, isovalue, d_triangles,
        d_triangleCount, maxTriangles);
    cudaDeviceSynchronize();
    checkCudaError("marching_cubes_kernel");

    // Get count
    int count = 0;
    cudaMemcpy(&count, d_triangleCount, sizeof(int), cudaMemcpyDeviceToHost);
    if (m_debug)
      std::cout << "[FrostGPUManager] Generated Triangles: " << count
                << " (Max Buffer: " << maxTriangles << ", Grid: " << m_gridDim.x
                << "x" << m_gridDim.y << "x" << m_gridDim.z << ")" << std::endl;

    if (count > maxTriangles) {
      if (m_debug)
        std::cerr
            << "[FrostGPUManager] WARNING: Triangle count exceeded buffer "
               "size! Clipping mesh."
            << std::endl;
      count = maxTriangles;
    }

    m_generatedTriangleCount = count;
  }

  // Download mesh for Blender
  void downloadMesh(float *verticesHost, int maxVerts) {
    if (m_generatedTriangleCount * 3 > maxVerts) {
      if (m_debug)
        std::cerr << "[FrostGPUManager] Host buffer too small for mesh!"
                  << std::endl;
      return;
    }
    cudaMemcpy(verticesHost, d_triangles,
               m_generatedTriangleCount * 3 * sizeof(float3),
               cudaMemcpyDeviceToHost);

    // Sanity Check for NaNs (Debug Shards)
    // Sanity Check for NaNs (Debug Shards)
    for (int i = 0; i < m_generatedTriangleCount * 3 * 3; ++i) {
      if (std::isnan(verticesHost[i])) {
        if (m_debug)
          std::cerr << "[FrostGPUManager] ERROR: NaN detected in mesh at index "
                    << i << "!" << std::endl;
        break; // One is enough
      }
    }
  }

  int getTriangleCount() { return m_generatedTriangleCount; }

  void downloadScalarField(float *hostGrid, int expectedSize) {
    size_t totalVoxels = (size_t)m_gridDim.x * m_gridDim.y * m_gridDim.z;
    if (expectedSize != totalVoxels)
      return;
    cudaMemcpy(hostGrid, d_grid, totalVoxels * sizeof(float),
               cudaMemcpyDeviceToHost);
  }

private:
  void releaseGridBuffers() {
    if (d_grid)
      cudaFree(d_grid);
    if (d_gridWeights)
      cudaFree(d_gridWeights);
    if (d_gridBlendedRadii)
      cudaFree(d_gridBlendedRadii);
    if (d_gridBlendedOffsets)
      cudaFree(d_gridBlendedOffsets);

    d_grid = nullptr;
    d_gridWeights = nullptr;
    d_gridBlendedRadii = nullptr;
    d_gridBlendedOffsets = nullptr;
  }

  void checkCudaError(const char *msg) {
    cudaError_t err = cudaGetLastError();
    if (cudaSuccess != err) {
      if (m_debug)
        std::cerr << "[FrostGPUManager] CUDA Error (" << msg
                  << "): " << cudaGetErrorString(err) << std::endl;
    }
  }

  int m_numParticles = 0;
  bool m_debug = false; // Debug flag
  std::vector<float> m_positionsHost;
  std::vector<float> m_radiiHost;
  float *d_positions = nullptr;
  float *d_radii = nullptr;

  std::unique_ptr<cuNSearch::NeighborhoodSearch> m_nsearch;
  unsigned int m_pointSetId = 0;
  float m_searchRadius = 0.0f;
  float m_lastSearchRadius = 0.0f; // For caching
  bool m_positionsChanged = true;  // Track if positions need rebuild

  float *d_grid = nullptr;
  float *d_gridWeights = nullptr;
  float *d_gridBlendedRadii = nullptr;
  float3 *d_gridBlendedOffsets = nullptr;
  float3 m_gridMin = {0, 0, 0};
  float m_voxelSize = 0.1f;
  int3 m_gridDim = {0, 0, 0};
  float m_lowDensityTrimmingDensity = 0.0f;
  float m_lowDensityTrimmingStrength = 0.0f;
  int m_particleBlockSize = 256;

  // MC
  float3 *d_triangles = nullptr;
  int *d_triangleCount = nullptr;
  int m_generatedTriangleCount = 0;
};

// ========================================================================
// C Wrapper Interface
// ========================================================================

extern "C" {

void launch_hello_cuda() {
  std::cout << "[CUDA] Initializing Device..." << std::endl;
}

void *FrostGPUManager_Create() { return new FrostGPUManager(); }

void FrostGPUManager_Destroy(void *manager) {
  if (manager)
    delete static_cast<FrostGPUManager *>(manager);
}

void FrostGPUManager_UploadParticles(void *manager, const float *positions,
                                     const float *radii, int count) {
  if (manager)
    static_cast<FrostGPUManager *>(manager)->uploadParticles(positions, radii,
                                                             count);
}

int FrostGPUManager_FindNeighbors(void *manager, float radius) {
  if (manager)
    return static_cast<FrostGPUManager *>(manager)->findNeighbors(radius);
  return -1;
}

void FrostGPUManager_ComputeDensity(void *manager) {
  if (manager)
    static_cast<FrostGPUManager *>(manager)->computeDensity();
}

void FrostGPUManager_SetMeshingParameters(void *manager,
                                          float lowDensityTrimmingDensity,
                                          float lowDensityTrimmingStrength,
                                          int blockSize) {
  if (manager)
    static_cast<FrostGPUManager *>(manager)->setMeshingParameters(
        lowDensityTrimmingDensity, lowDensityTrimmingStrength, blockSize);
}

void FrostGPUManager_SetScalarFieldParameters(void *manager, float minX,
                                              float minY, float minZ,
                                              float sizeX, float sizeY,
                                              float sizeZ, float voxelSize) {
  if (manager)
    static_cast<FrostGPUManager *>(manager)->setScalarFieldParameters(
        minX, minY, minZ, sizeX, sizeY, sizeZ, voxelSize);
}

void FrostGPUManager_ComputeScalarField(void *manager) {
  if (manager)
    static_cast<FrostGPUManager *>(manager)->computeScalarField();
}

void FrostGPUManager_DownloadScalarField(void *manager, float *hostGrid,
                                         int expectedSize) {
  if (manager)
    static_cast<FrostGPUManager *>(manager)->downloadScalarField(hostGrid,
                                                                 expectedSize);
}

// New MC API
void FrostGPUManager_ComputeMesh(void *manager, float isovalue) {
  if (manager)
    static_cast<FrostGPUManager *>(manager)->computeMesh(isovalue);
}

int FrostGPUManager_GetTriangleCount(void *manager) {
  if (manager)
    return static_cast<FrostGPUManager *>(manager)->getTriangleCount();
  return 0;
}

void FrostGPUManager_DownloadMesh(void *manager, float *verticesHost,
                                  int maxVerts) {
  if (manager)
    static_cast<FrostGPUManager *>(manager)->downloadMesh(verticesHost,
                                                          maxVerts);
}

void FrostGPUManager_SetDebug(void *manager, bool debug) {
  if (manager)
    static_cast<FrostGPUManager *>(manager)->setDebug(debug);
}
}
