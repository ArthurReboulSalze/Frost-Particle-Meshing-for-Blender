#include "frost_interface.hpp"

#include <frantic/channels/channel_propagation_policy.hpp> // Added for evolve_mesh
#include <frantic/geometry/trimesh3.hpp>
#include <frantic/logging/progress_logger.hpp>
#include <frantic/particles/particle_array.hpp>
#include <frantic/particles/streams/particle_array_particle_istream.hpp>
#include <frost/frost.hpp>

#include <algorithm>                       // For std::sort, std::unique
#include <cmath>                           // for std::round
#include <frantic/geometry/relaxation.hpp> // Added for laplacian_smooth
#include <frantic/graphics/boundbox3f.hpp>
#include <frantic/volumetrics/implicitsurface/particle_implicit_surface_policies.hpp>
#include <frantic/volumetrics/voxel_coord_system.hpp>
#include <frost/frost_parameters.hpp>
#include <iostream>
#include <tbb/parallel_for.h> // For parallel mesh copy
#include <unordered_map>

namespace {
struct IntKey {
  int64_t x, y, z;
  bool operator==(const IntKey &o) const {
    return x == o.x && y == o.y && z == o.z;
  }
};

struct IntKeyHash {
  size_t operator()(const IntKey &k) const {
    // Simple hash combining
    size_t h = std::hash<int64_t>{}(k.x);
    h ^= std::hash<int64_t>{}(k.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<int64_t>{}(k.z) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
  }
};

inline IntKey makeIntKey(float x, float y, float z, float weldGridScale) {
  return {static_cast<int64_t>(std::round(x * weldGridScale)),
          static_cast<int64_t>(std::round(y * weldGridScale)),
          static_cast<int64_t>(std::round(z * weldGridScale))};
}

} // namespace

// CUDA Hooks
#ifdef FROST_ENABLE_CUDA
extern "C" void launch_hello_cuda();
extern "C" void *FrostGPUManager_Create();
extern "C" void FrostGPUManager_Destroy(void *manager);
extern "C" void FrostGPUManager_UploadParticles(void *manager,
                                                const float *positions,
                                                const float *radii,
                                                int count);
extern "C" int FrostGPUManager_FindNeighbors(void *manager, float radius);
extern "C" void FrostGPUManager_ComputeDensity(void *manager);
extern "C" void FrostGPUManager_SetMeshingParameters(
    void *manager, float lowDensityTrimmingDensity,
    float lowDensityTrimmingStrength, int blockSize);
extern "C" void
FrostGPUManager_SetScalarFieldParameters(void *manager, float minX, float minY,
                                         float minZ, float sizeX, float sizeY,
                                         float sizeZ, float voxelSize);
extern "C" void FrostGPUManager_ComputeScalarField(void *manager);
extern "C" void FrostGPUManager_DownloadScalarField(void *manager,
                                                    float *hostGrid,
                                                    int expectedSize);
extern "C" void FrostGPUManager_ComputeMesh(void *manager, float isovalue);
extern "C" int FrostGPUManager_GetTriangleCount(void *manager);
extern "C" void FrostGPUManager_DownloadMesh(void *manager, float *verticesHost,
                                             int maxVerts);
extern "C" void FrostGPUManager_SetDebug(void *manager, bool debug);
#endif

namespace {

// Build edge-use map to identify non-manifold and boundary edges
// Returns true if mesh is fully manifold (no boundary edges)
bool is_manifold_closed(const frantic::geometry::trimesh3 &mesh) {
  // Simple edge use counting
  std::unordered_map<int64_t, int> edgeUseCount;

  auto makeEdgeKey = [](int v0, int v1) -> int64_t {
    if (v0 > v1)
      std::swap(v0, v1);
    return ((int64_t)v0 << 32) | (int64_t)v1;
  };

  for (size_t f = 0; f < mesh.face_count(); ++f) {
    frantic::graphics::vector3 face = mesh.get_face(f);
    int v0 = (int)face.x, v1 = (int)face.y, v2 = (int)face.z;
    edgeUseCount[makeEdgeKey(v0, v1)]++;
    edgeUseCount[makeEdgeKey(v1, v2)]++;
    edgeUseCount[makeEdgeKey(v2, v0)]++;
  }

  // Check for non-manifold or boundary edges
  for (const auto &kv : edgeUseCount) {
    if (kv.second != 2)
      return false; // Not exactly 2 faces sharing edge
  }
  return true;
}

// Safe Laplacian smooth that skips boundary vertices to prevent holes
void safe_laplacian_smooth(frantic::geometry::trimesh3 &mesh, int iterations,
                           float strength) {
  if (iterations <= 0 || mesh.vertex_count() == 0 || mesh.face_count() == 0)
    return;

  // Build edge-use map
  std::unordered_map<int64_t, int> edgeUseCount;
  auto makeEdgeKey = [](int v0, int v1) -> int64_t {
    if (v0 > v1)
      std::swap(v0, v1);
    return ((int64_t)v0 << 32) | (int64_t)v1;
  };

  for (size_t f = 0; f < mesh.face_count(); ++f) {
    frantic::graphics::vector3 face = mesh.get_face(f);
    int v0 = (int)face.x, v1 = (int)face.y, v2 = (int)face.z;
    edgeUseCount[makeEdgeKey(v0, v1)]++;
    edgeUseCount[makeEdgeKey(v1, v2)]++;
    edgeUseCount[makeEdgeKey(v2, v0)]++;
  }

  // Mark boundary vertices (vertices on boundary edges)
  std::vector<bool> isBoundary(mesh.vertex_count(), false);
  for (const auto &kv : edgeUseCount) {
    if (kv.second != 2) { // Boundary or non-manifold edge
      int v0 = (int)(kv.first >> 32);
      int v1 = (int)(kv.first & 0xFFFFFFFF);
      if (v0 >= 0 && v0 < (int)mesh.vertex_count())
        isBoundary[v0] = true;
      if (v1 >= 0 && v1 < (int)mesh.vertex_count())
        isBoundary[v1] = true;
    }
  }

  // If no boundary vertices found, mesh is fully closed - use fast path
  bool hasBoundary = false;
  for (size_t i = 0; i < mesh.vertex_count() && !hasBoundary; ++i) {
    if (isBoundary[i])
      hasBoundary = true;
  }

  if (!hasBoundary) {
    // Mesh is fully closed, use Frantic's optimized version
    frantic::geometry::relaxation::laplacian_smooth(mesh, iterations, strength);
    return;
  }

  // Build vertex adjacency
  std::vector<std::vector<int>> vertexNeighbors(mesh.vertex_count());
  for (size_t f = 0; f < mesh.face_count(); ++f) {
    frantic::graphics::vector3 face = mesh.get_face(f);
    int v0 = (int)face.x, v1 = (int)face.y, v2 = (int)face.z;
    vertexNeighbors[v0].push_back(v1);
    vertexNeighbors[v0].push_back(v2);
    vertexNeighbors[v1].push_back(v0);
    vertexNeighbors[v1].push_back(v2);
    vertexNeighbors[v2].push_back(v0);
    vertexNeighbors[v2].push_back(v1);
  }

  // Remove duplicates from neighbor lists
  for (auto &neighbors : vertexNeighbors) {
    std::sort(neighbors.begin(), neighbors.end());
    neighbors.erase(std::unique(neighbors.begin(), neighbors.end()),
                    neighbors.end());
  }

  // Perform smoothing iterations
  std::vector<frantic::graphics::vector3f> newPositions(mesh.vertex_count());

  for (int iter = 0; iter < iterations; ++iter) {
    // Copy current positions
    for (size_t i = 0; i < mesh.vertex_count(); ++i) {
      newPositions[i] = mesh.get_vertex(i);
    }

    // Smooth only non-boundary vertices, using ALL neighbors for averaging
    tbb::parallel_for(tbb::blocked_range<size_t>(0, mesh.vertex_count()),
                      [&](const tbb::blocked_range<size_t> &r) {
                        for (size_t i = r.begin(); i != r.end(); ++i) {
                          // Skip boundary vertices - they don't move
                          if (isBoundary[i])
                            continue;

                          // Skip vertices with too few neighbors
                          if (vertexNeighbors[i].size() < 3)
                            continue;

                          // Average ALL neighbors (including boundary if any)
                          frantic::graphics::vector3f avg(0, 0, 0);
                          for (int n : vertexNeighbors[i]) {
                            avg += mesh.get_vertex(n);
                          }
                          avg /= (float)vertexNeighbors[i].size();

                          // Blend toward average
                          newPositions[i] =
                              mesh.get_vertex(i) * (1.0f - strength) +
                              avg * strength;
                        }
                      });

    // Apply new positions
    for (size_t i = 0; i < mesh.vertex_count(); ++i) {
      mesh.get_vertex(i) = newPositions[i];
    }
  }
}

void apply_push(frantic::geometry::trimesh3 &mesh, float distance) {
  if (std::abs(distance) < 1e-5f)
    return;

  mesh.build_vertex_normals();

  if (!mesh.has_vertex_channel(_T("Normal")))
    return;

  auto normals = mesh.get_vertex_channel_accessor<frantic::graphics::vector3f>(
      _T("Normal"));

  for (size_t i = 0; i < mesh.vertex_count(); ++i) {
    frantic::graphics::vector3f n = normals[i];
    float magSq = n.get_magnitude_squared();

    // Skip invalid normals
    if (magSq < 1e-12f || std::isnan(magSq) || std::isinf(magSq))
      continue;

    // Normalize the normal vector before applying push
    float mag = std::sqrt(magSq);
    n /= mag;

    // Apply push along normalized normal
    mesh.get_vertex(i) += n * distance;
  }
}

void refine_gpu_mesh_with_zhu_bridson_surface(
    frantic::geometry::trimesh3 &mesh,
    const frantic::particles::particle_array &particles,
    const frost_parameters &params, float gpuSearchRadiusScale,
    int refinementIterations, bool showDebugLog) {
  if (refinementIterations <= 0 || mesh.vertex_count() == 0 ||
      particles.size() == 0) {
    return;
  }

  const auto &channelMap = particles.get_channel_map();
  if (!channelMap.has_channel(_T("Position")) ||
      !channelMap.has_channel(_T("Radius"))) {
    return;
  }

  auto positionAccessor =
      channelMap.get_accessor<frantic::graphics::vector3f>(_T("Position"));
  auto radiusAccessor = channelMap.get_accessor<float>(_T("Radius"));

  float maxParticleRadius = 0.0f;
  frantic::graphics::boundbox3f particleBounds;
  bool hasValidParticle = false;

  for (size_t i = 0; i < particles.size(); ++i) {
    const char *particle = particles[i];
    const frantic::graphics::vector3f pos = positionAccessor.get(particle);
    const float radius = radiusAccessor.get(particle);
    if (!pos.is_finite() || radius <= 0.0f) {
      continue;
    }

    particleBounds += pos;
    maxParticleRadius = std::max(maxParticleRadius, radius);
    hasValidParticle = true;
  }

  if (!hasValidParticle || maxParticleRadius <= 0.0f) {
    return;
  }

  float voxelSize = 0.1f;
  if (params.get_meshing_resolution_mode() == 0) {
    float resolution = params.get_meshing_resolution();
    if (resolution < 0.001f)
      resolution = 0.001f;
    voxelSize = maxParticleRadius / resolution;
  } else {
    voxelSize = params.get_meshing_voxel_length();
  }
  voxelSize = std::max(voxelSize, 0.002f);

  const float effectRadiusScale =
      gpuSearchRadiusScale > 0.0f ? gpuSearchRadiusScale
                                  : params.get_zhu_bridson_blend_radius_scale();
  const float searchRadius = maxParticleRadius * effectRadiusScale;
  if (searchRadius <= 0.0f) {
    return;
  }

  frantic::graphics::vector3f gridMin =
      particleBounds.minimum() - frantic::graphics::vector3f(searchRadius);
  gridMin.x = std::floor(gridMin.x / voxelSize) * voxelSize;
  gridMin.y = std::floor(gridMin.y / voxelSize) * voxelSize;
  gridMin.z = std::floor(gridMin.z / voxelSize) * voxelSize;

  const frantic::graphics::vector3f meshingOrigin =
      gridMin - frantic::graphics::vector3f(voxelSize * 0.5f);
  frantic::volumetrics::voxel_coord_system meshingVCS(meshingOrigin,
                                                      voxelSize);

  const float particleVoxelLength =
      std::max(searchRadius, 0.5f * voxelSize);
  frantic::volumetrics::voxel_coord_system particleVCS(
      frantic::graphics::vector3f(0), particleVoxelLength);

  frantic::particles::particle_grid_tree particleTree;
  particleTree.reset(channelMap, particleVCS);

  for (size_t i = 0; i < particles.size(); ++i) {
    const char *particle = particles[i];
    const frantic::graphics::vector3f pos = positionAccessor.get(particle);
    const float radius = radiusAccessor.get(particle);
    if (!pos.is_finite() || radius <= 0.0f) {
      continue;
    }
    particleTree.insert(particle);
  }

  const bool enableLowDensityTrimming =
      params.get_zhu_bridson_enable_low_density_trimming();
  const float lowDensityTrimmingDensity =
      enableLowDensityTrimming
          ? params.get_zhu_bridson_low_density_trimming_threshold()
          : 0.0f;
  const float lowDensityTrimmingStrength =
      enableLowDensityTrimming
          ? params.get_zhu_bridson_low_density_trimming_strength()
          : 0.0f;

  frantic::volumetrics::implicitsurface::particle_zhu_bridson_is_policy policy(
      particleTree, maxParticleRadius, effectRadiusScale,
      lowDensityTrimmingDensity, lowDensityTrimmingStrength, meshingVCS,
      refinementIterations);

  const float gradientStep = std::max(voxelSize * 0.5f, 1e-4f);
  const float maxStep = std::max(voxelSize, 1e-4f);
  const float convergence = std::max(voxelSize * 1e-3f, 1e-5f);

  for (size_t i = 0; i < mesh.vertex_count(); ++i) {
    frantic::graphics::vector3f v = mesh.get_vertex(i);

    for (int iter = 0; iter < refinementIterations; ++iter) {
      const float density = policy.get_density(v);
      if (!std::isfinite(density) || std::abs(density) <= convergence) {
        break;
      }

      frantic::graphics::vector3f gradient =
          policy.get_gradient(v, gradientStep);
      const float gradMagSq = gradient.get_magnitude_squared();
      if (!std::isfinite(gradMagSq) || gradMagSq < 1e-12f) {
        break;
      }

      frantic::graphics::vector3f step = gradient * (density / gradMagSq);
      const float stepMagSq = step.get_magnitude_squared();
      if (!std::isfinite(stepMagSq) || stepMagSq < 1e-16f) {
        break;
      }

      const float stepMag = std::sqrt(stepMagSq);
      if (stepMag > maxStep) {
        step *= maxStep / stepMag;
      }

      v -= step;
    }

    mesh.get_vertex(i) = v;
  }

  if (showDebugLog) {
    std::cout << "[CUDA] Surface refinement projected " << mesh.vertex_count()
              << " vertices with " << refinementIterations
              << " Zhu-Bridson iteration(s)." << std::endl;
  }
}
} // namespace

namespace py = pybind11;

struct FrostInterface::Impl {
  frost_parameters params;
  frantic::particles::particle_array particles;

  // Default constructor
  Impl() {
    // Set defaults
    params.set_meshing_resolution(0.1f);
    // Default channel map
    frantic::channels::channel_map map;
    map.define_channel<frantic::graphics::vector3f>(_T("Position"));
    map.define_channel<float>(_T("Radius"));
    map.define_channel<frantic::graphics::vector3f>(_T("Velocity"));
    map.end_channel_definition();
    particles.set_channel_map(map);
  }

  // Custom parameters for manual calls
  int relax_iterations = 0;
  float relax_strength = 0.5f;
  float push_distance = 0.0f;
  bool enable_gpu = false;
  float gpu_search_radius_scale = 2.0f;
  float gpu_voxel_size =
      0.1f; // GPU-specific voxel size (controls mesh resolution)
  int gpu_block_size = 256;
  int gpu_surface_refinement = 0;
  bool show_debug_log = false; // Debug toggle

  // GPU Manager
  void *gpuManager = nullptr;

  ~Impl() {
#ifdef FROST_ENABLE_CUDA
    if (gpuManager) {
      FrostGPUManager_Destroy(gpuManager);
      gpuManager = nullptr;
    }
#endif
  }
};

FrostInterface::FrostInterface() : m_impl(std::make_unique<Impl>()) {}
FrostInterface::~FrostInterface() = default;

void FrostInterface::set_particles(py::array_t<float> positions,
                                   py::array_t<float> radii,
                                   py::object velocities_obj) {
  auto r_pos = positions.unchecked<2>();
  auto r_radius = radii.unchecked<1>();

  size_t count = positions.shape(0);

  // Check velocity
  bool has_velocity = !velocities_obj.is_none();

  // Define channel map
  frantic::channels::channel_map map;
  map.define_channel<frantic::graphics::vector3f>(_T("Position"));
  map.define_channel<float>(_T("Radius"));
  if (has_velocity) {
    map.define_channel<frantic::graphics::vector3f>(_T("Velocity"));
  }
  map.end_channel_definition();

  m_impl->particles.reset(map);
  m_impl->particles.resize(count); // Use resize instead of reserve+add

  // Accessors
  frantic::channels::channel_accessor<frantic::graphics::vector3f> acc_pos =
      map.get_accessor<frantic::graphics::vector3f>(_T("Position"));
  frantic::channels::channel_accessor<float> acc_rad =
      map.get_accessor<float>(_T("Radius"));

  // Fill basic data - PARALLELIZED with TBB
  tbb::parallel_for(tbb::blocked_range<size_t>(0, count),
                    [&](const tbb::blocked_range<size_t> &r) {
                      for (size_t i = r.begin(); i != r.end(); ++i) {
                        char *p = m_impl->particles.at(i);
                        acc_pos(p) = frantic::graphics::vector3f(
                            r_pos(i, 0), r_pos(i, 1), r_pos(i, 2));
                        acc_rad(p) = r_radius(i);
                      }
                    });

  // Fill velocity if present - PARALLELIZED
  if (has_velocity) {
    frantic::channels::channel_accessor<frantic::graphics::vector3f> acc_vel =
        map.get_accessor<frantic::graphics::vector3f>(_T("Velocity"));
    py::array_t<float> velocities = velocities_obj.cast<py::array_t<float>>();
    auto r_vel = velocities.unchecked<2>();

    tbb::parallel_for(tbb::blocked_range<size_t>(0, count),
                      [&](const tbb::blocked_range<size_t> &r) {
                        for (size_t i = r.begin(); i != r.end(); ++i) {
                          char *p = m_impl->particles.at(i);
                          acc_vel(p) = frantic::graphics::vector3f(
                              r_vel(i, 0), r_vel(i, 1), r_vel(i, 2));
                        }
                      });
  }
}

void FrostInterface::set_parameter(const std::string &name, py::object value) {
  auto &p = m_impl->params;

  try {
    if (name == "meshing_method")
      p.set_meshing_method(value.cast<int>());
    else if (name == "resolution")
      p.set_meshing_resolution(value.cast<float>());
    else if (name ==
             "meshing_resolution_mode") // Fixed name from resolution_mode
      p.set_meshing_resolution_mode(value.cast<int>());
    else if (name == "voxel_size") // Matches Python
      p.set_meshing_voxel_length(value.cast<float>());
    else if (name == "relax_iterations")
      m_impl->relax_iterations = value.cast<int>();
    else if (name == "relax_strength")
      m_impl->relax_strength = value.cast<float>();
    else if (name == "push_distance")
      m_impl->push_distance = value.cast<float>();
    else if (name == "use_gpu")
      m_impl->enable_gpu = value.cast<bool>();
    else if (name == "gpu_search_radius_scale")
      m_impl->gpu_search_radius_scale = value.cast<float>();
    else if (name == "gpu_voxel_size")
      m_impl->gpu_voxel_size = value.cast<float>();
    else if (name == "gpu_block_size")
      m_impl->gpu_block_size = value.cast<int>();
    else if (name == "gpu_surface_refinement")
      m_impl->gpu_surface_refinement = value.cast<int>();
    else if (name == "show_debug_log")
      m_impl->show_debug_log = value.cast<bool>();

    // Union of Spheres handled in Python via radius scaling

    // Metaball
    else if (name == "metaball_radius_scale")
      p.set_metaball_radius_scale(value.cast<float>());
    else if (name == "metaball_isosurface_level")
      p.set_metaball_isosurface_level(value.cast<float>());
    // Plain Marching Cubes
    else if (name == "plain_marching_cubes_radius_scale")
      p.set_plain_marching_cubes_radius_scale(value.cast<float>());
    else if (name == "plain_marching_cubes_isovalue")
      p.set_plain_marching_cubes_isovalue(value.cast<float>());
    // Zhu-Bridson
    else if (name == "zhu_bridson_blend_radius_scale")
      p.set_zhu_bridson_blend_radius_scale(value.cast<float>());
    else if (name == "zhu_bridson_low_density_trimming")
      p.set_zhu_bridson_enable_low_density_trimming(value.cast<bool>());
    else if (name == "zhu_bridson_trimming_threshold")
      p.set_zhu_bridson_low_density_trimming_threshold(value.cast<float>());
    else if (name == "zhu_bridson_trimming_strength")
      p.set_zhu_bridson_low_density_trimming_strength(value.cast<float>());
    // Anisotropic
    else if (name == "anisotropic_radius_scale")
      p.set_anisotropic_radius_scale(value.cast<float>());
    else if (name == "anisotropic_max_anisotropy")
      p.set_anisotropic_max_anisotropy(value.cast<float>());

    // Adaptive Resolution / Vertex Refinement
    else if (name == "vert_refinement")
      p.set_vert_refinement_iterations(value.cast<int>());

    // ... more checks
  } catch (const std::exception &e) {
    // Log error?
  }
}

void FrostInterface::set_parameters(py::dict params) {
  for (auto item : params) {
    std::string key = py::str(item.first);
    py::object value = py::reinterpret_borrow<py::object>(item.second);
    set_parameter(key, value);
  }
}

#include <boost/make_shared.hpp> // Added for make_shared

std::tuple<py::array_t<float>, py::array_t<int>>
FrostInterface::generate_mesh() {
  frantic::geometry::trimesh3 outMethod;
  bool gpuSuccess = false;
#ifdef FROST_ENABLE_CUDA
  if (m_impl->enable_gpu) {
    try {
      if (m_impl->show_debug_log)
           std::cout << "[CUDA] GPU Mode Enabled - Using cuNSearch" << std::endl;

      // Initialize Manager if needed
      if (!m_impl->gpuManager) {
        m_impl->gpuManager = FrostGPUManager_Create();
      }
      
      // Update Debug State in Manager
      FrostGPUManager_SetDebug(m_impl->gpuManager, m_impl->show_debug_log);

      // Get particle count and positions for GPU neighbor search
      size_t numParticles = m_impl->particles.size();
      if (numParticles > 0) {
        // 0. Extract positions, find Max Radius, and Compute Bounds
        float maxParticleRadius = 0.1f; // Default fallback
        bool foundMaxRadius = false;
        std::vector<float> positionsFlat(numParticles * 3);
        std::vector<float> radiiFlat(numParticles, maxParticleRadius);
        frantic::graphics::boundbox3f bounds;

        {
          // Check for Radius channel
          auto &cmap = m_impl->particles.get_channel_map();
          bool hasRadius = cmap.has_channel(_T("Radius"));
          frantic::channels::channel_accessor<float> radAccessor;
          if (hasRadius)
            radAccessor = cmap.get_accessor<float>(_T("Radius"));

          auto posAccessor =
              cmap.get_accessor<frantic::graphics::vector3f>(_T("Position"));

          // Single loop for extraction, max radius finding, and bounds
          for (size_t i = 0; i < numParticles; ++i) {
            char *particle = m_impl->particles.at(i);
            frantic::graphics::vector3f pos = posAccessor.get(particle);

            // Fill flat array
            positionsFlat[i * 3 + 0] = pos.x;
            positionsFlat[i * 3 + 1] = pos.y;
            positionsFlat[i * 3 + 2] = pos.z;

            // Update Bounds
            bounds += pos;

            if (hasRadius) {
              float r = radAccessor.get(particle);
              radiiFlat[i] = r;
              if (!foundMaxRadius || r > maxParticleRadius) {
                maxParticleRadius = r;
                foundMaxRadius = true;
              }
            } else {
              radiiFlat[i] = maxParticleRadius;
            }
          }

          // Upload to GPU
          FrostGPUManager_UploadParticles(m_impl->gpuManager,
                                          positionsFlat.data(),
                                          radiiFlat.data(),
                                          static_cast<int>(numParticles));
        }

        // 1. Determine Voxel Size based on Resolution Mode (Unified with CPU
        // logic)
        float voxelSize = 0.1f;
        int resMode =
            m_impl->params
                .get_meshing_resolution_mode(); // 0=Subdivide, 1=Fixed

        if (resMode == 0) { // Subdivide Max Radius
          float resolution = m_impl->params.get_meshing_resolution();
          if (resolution < 0.001f)
            resolution = 0.001f;
          voxelSize = maxParticleRadius / resolution;
        } else { // Fixed Voxel Length
          voxelSize = m_impl->params.get_meshing_voxel_length();
        }

        // Safety clamp for Voxel Size (prevent GPU crash on ultra-dense grids)
        if (voxelSize < 0.002f)
          voxelSize = 0.002f;

        // 2. Determine Search Radius
        // FIXED: Search Radius is relative to Particle Radius, NOT Resolution
        // count. gpu_search_radius_scale is now a multiplier of
        // maxParticleRadius (e.g., 2.0x)
        float searchRadius =
            maxParticleRadius * m_impl->gpu_search_radius_scale;

        // Perform GPU neighbor search
        int result =
            FrostGPUManager_FindNeighbors(m_impl->gpuManager, searchRadius);

        if (result == 0) {
          if (m_impl->show_debug_log)
               std::cout << "[CUDA] Neighbor search completed successfully"
                    << std::endl;

          const bool enableLowDensityTrimming =
              m_impl->params.get_zhu_bridson_enable_low_density_trimming();
          const float lowDensityTrimmingDensity =
              enableLowDensityTrimming
                  ? m_impl->params
                        .get_zhu_bridson_low_density_trimming_threshold()
                  : 0.0f;
          const float lowDensityTrimmingStrength =
              enableLowDensityTrimming
                  ? m_impl->params.get_zhu_bridson_low_density_trimming_strength()
                  : 0.0f;

          FrostGPUManager_SetMeshingParameters(
              m_impl->gpuManager, lowDensityTrimmingDensity,
              lowDensityTrimmingStrength, m_impl->gpu_block_size);

          // Preserved as a pipeline stage for compatibility. The Zhu-Bridson
          // field is now built during ComputeScalarField.
          FrostGPUManager_ComputeDensity(m_impl->gpuManager);

          // Compute Scalar Field (Splatting)
          // Bounds are already computed above (in extraction loop)

          float max_radius = 0.0f;
          // Use searchRadius as max influence for padding
          max_radius = searchRadius;

          // Pad bounds to ensure all influence is captured
          frantic::graphics::vector3f minPt =
              bounds.minimum() - frantic::graphics::vector3f(max_radius);
          frantic::graphics::vector3f maxPt =
              bounds.maximum() + frantic::graphics::vector3f(max_radius);

          // Use VALIDATED voxelSize (calculated above)
          // Old override removed

          // STABILITY FIX v2: Snap grid origin to WORLD ORIGIN (0,0,0) aligned
          // voxels This ensures consistent grid positioning regardless of voxel
          // size The grid origin is always a multiple of voxelSize from world
          // origin
          minPt.x = std::floor(minPt.x / voxelSize) * voxelSize;
          minPt.y = std::floor(minPt.y / voxelSize) * voxelSize;
          minPt.z = std::floor(minPt.z / voxelSize) * voxelSize;

          // Snap maxPt to voxel boundary (ceiling) for consistent coverage
          maxPt.x = std::ceil(maxPt.x / voxelSize) * voxelSize;
          maxPt.y = std::ceil(maxPt.y / voxelSize) * voxelSize;
          maxPt.z = std::ceil(maxPt.z / voxelSize) * voxelSize;

          // Recalculate size from snapped bounds
          frantic::graphics::vector3f size = maxPt - minPt;

          // Set Grid Params
          FrostGPUManager_SetScalarFieldParameters(m_impl->gpuManager, minPt.x,
                                                   minPt.y, minPt.z, size.x,
                                                   size.y, size.z, voxelSize);

          // Compute Field
          FrostGPUManager_ComputeScalarField(m_impl->gpuManager);

          if (m_impl->show_debug_log)
               std::cout << "[CUDA] Scalar Field Computed." << std::endl;

          // Compute Mesh
          float isovalue = 0.0f;
          FrostGPUManager_ComputeMesh(m_impl->gpuManager, isovalue);

          int triCount = FrostGPUManager_GetTriangleCount(m_impl->gpuManager);
          if (triCount > 0) {
            std::vector<float> rawVertices(triCount * 3 *
                                           3); // 3 verts per tri * 3 coords
            FrostGPUManager_DownloadMesh(m_impl->gpuManager, rawVertices.data(),
                                         (int)rawVertices.size());

            if (m_impl->show_debug_log)
                 std::cout << "[CUDA] Welding vertices..." << std::endl;

            // Debug first triangle to check values
            if (rawVertices.size() >= 9 && m_impl->show_debug_log) {
              std::cout << "Raw V0: " << rawVertices[0] << ", "
                        << rawVertices[1] << ", " << rawVertices[2]
                        << std::endl;
              std::cout << "Raw V1: " << rawVertices[3] << ", "
                        << rawVertices[4] << ", " << rawVertices[5]
                        << std::endl;
              std::cout << "Raw V2: " << rawVertices[6] << ", "
                        << rawVertices[7] << ", " << rawVertices[8]
                        << std::endl;
            }

            std::unordered_map<IntKey, int, IntKeyHash> vertMap;
            std::vector<float> weldedVertices;
            std::vector<int> weldedFaces;
            int nextIdx = 0;
            const float weldTolerance =
                std::max(voxelSize * 0.0025f, 1e-5f);
            const float weldGridScale = 1.0f / weldTolerance;

            // Reserve approximate memory to avoid reallocs
            weldedVertices.reserve(triCount * 3 *
                                   3); // Max possible unique verts
            weldedFaces.reserve(triCount * 3);

            for (int t = 0; t < triCount; ++t) {
              int indices[3];
              frantic::graphics::vector3f verts[3];

              // Process each vertex of the triangle
              for (int j = 0; j < 3; ++j) {
                float vx = rawVertices[(t * 3 + j) * 3 + 0];
                float vy = rawVertices[(t * 3 + j) * 3 + 1];
                float vz = rawVertices[(t * 3 + j) * 3 + 2];
                verts[j] = frantic::graphics::vector3f(vx, vy, vz);

                IntKey k = makeIntKey(vx, vy, vz, weldGridScale);
                auto it = vertMap.find(k);
                if (it != vertMap.end()) {
                  indices[j] = it->second;
                } else {
                  int idx = nextIdx++;
                  vertMap[k] = idx;
                  indices[j] = idx;
                  weldedVertices.push_back(vx);
                  weldedVertices.push_back(vy);
                  weldedVertices.push_back(vz);
                }
              }

              // Filter Degenerate Triangles
              // 1. Topological (duplicate indices)
              if (indices[0] == indices[1] || indices[1] == indices[2] ||
                  indices[0] == indices[2]) {
                continue;
              }

              // Valid triangle -> Add to faces
              weldedFaces.push_back(indices[0]);
              weldedFaces.push_back(indices[1]);
              weldedFaces.push_back(indices[2]);
            }

            int num_verts = (int)weldedVertices.size() / 3;
            int num_faces = (int)weldedFaces.size() / 3;

            if (m_impl->show_debug_log)
                 std::cout << "[CUDA] Welded Mesh: " << num_verts << " vertices, "
                       << num_faces << " faces." << std::endl;

            // Fill outMethod for downstream post-processing
            outMethod.set_vertex_count(num_verts);
            outMethod.set_face_count(num_faces);

            // Parallel copy vertices to outMethod
            tbb::parallel_for(
                tbb::blocked_range<size_t>(0, num_verts),
                [&](const tbb::blocked_range<size_t> &r) {
                  for (size_t i = r.begin(); i != r.end(); ++i) {
                    outMethod.get_vertex(i) = frantic::graphics::vector3f(
                        weldedVertices[i * 3 + 0], weldedVertices[i * 3 + 1],
                        weldedVertices[i * 3 + 2]);
                  }
                });

            // Parallel copy faces to outMethod
            tbb::parallel_for(tbb::blocked_range<size_t>(0, num_faces),
                              [&](const tbb::blocked_range<size_t> &r) {
                                for (size_t i = r.begin(); i != r.end(); ++i) {
                                  outMethod.get_face(i) =
                                      frantic::graphics::vector3(
                                          (float)weldedFaces[i * 3 + 0],
                                          (float)weldedFaces[i * 3 + 1],
                                          (float)weldedFaces[i * 3 + 2]);
                                }
                              });

            gpuSuccess = true;
            // Continue to post-processing...
          } else {
            if (m_impl->show_debug_log)
                 std::cout << "[CUDA] No triangles generated." << std::endl;
          }
        } else {

          if (m_impl->show_debug_log)
               std::cerr << "[CUDA] GPU neighbor search failed" << std::endl;
        }
      }
    } catch (const std::exception &e) {
      std::cerr << "[CUDA] Exception in GPU path: " << e.what() << std::endl;
    } catch (...) {
      std::cerr << "[CUDA] Unknown exception in GPU path" << std::endl;
    }
    // Explicitly return empty mesh if GPU failed or finished without returning
    // specific data This prevents falling back to CPU solver when GPU was
    // requested
    // return std::make_tuple(py::array_t<float>(), py::array_t<int>());
    // Allow fallthrough to post-processing (outMethod will be empty if failed)
  }
#endif
  // frantic::geometry::trimesh3 outMethod; // ALREADY DECLARED AT TOP
  frantic::logging::null_progress_logger logger;

  // Create stream
  auto stream = boost::make_shared<
      frantic::particles::streams::particle_array_particle_istream>(
      m_impl->particles);

  std::size_t particleCount = 0;

  // Release Python GIL for true multi-threading during mesh generation
  {
    py::gil_scoped_release release;

    // Run Frost (parallelized via TBB) - Only if GPU didn't already generate
    // mesh
    if (!gpuSuccess) {
      frost::build_trimesh3(outMethod, particleCount, m_impl->params, stream,
                            logger);
    }

    if (gpuSuccess && m_impl->gpu_surface_refinement > 0) {
      refine_gpu_mesh_with_zhu_bridson_surface(
          outMethod, m_impl->particles, m_impl->params,
          m_impl->gpu_search_radius_scale, m_impl->gpu_surface_refinement,
          m_impl->show_debug_log);
    }

    // Post-Processing Pipeline (also parallelized)

    // 1. Push (Inflate/Deflate)
    if (std::abs(m_impl->push_distance) > 1e-5f) {
      apply_push(outMethod, m_impl->push_distance);
    }

    // 2. Relax (Laplacian Smoothing) - uses hybrid approach:
    // - If mesh is fully closed: uses Frantic's optimized version
    // - If mesh has boundary edges: uses safe version that freezes boundary
    if (m_impl->relax_iterations > 0) {
      safe_laplacian_smooth(outMethod, m_impl->relax_iterations,
                            m_impl->relax_strength);
    }
  } // GIL re-acquired here

  // Convert output - Optimized for performance
  size_t vertCount = outMethod.vertex_count();
  size_t faceCount = outMethod.face_count();

  py::array_t<float> vertices({(py::ssize_t)vertCount, (py::ssize_t)3});
  py::array_t<int> faces({(py::ssize_t)faceCount, (py::ssize_t)3});

  // Get direct pointers to Numpy arrays (zero-copy where possible)
  float *verts_ptr = vertices.mutable_data();
  int *faces_ptr = faces.mutable_data();

  // Release GIL for the parallel copy to allow other Python threads to run
  {
    py::gil_scoped_release release_copy;

    // Parallel copy of vertices using TBB
    tbb::parallel_for(tbb::blocked_range<size_t>(0, vertCount),
                      [&](const tbb::blocked_range<size_t> &r) {
                        for (size_t i = r.begin(); i != r.end(); ++i) {
                          frantic::graphics::vector3f v =
                              outMethod.get_vertex(i);
                          size_t idx = i * 3;
                          verts_ptr[idx + 0] = v.x;
                          verts_ptr[idx + 1] = v.y;
                          verts_ptr[idx + 2] = v.z;
                        }
                      });

    // Parallel copy of faces using TBB
    tbb::parallel_for(tbb::blocked_range<size_t>(0, faceCount),
                      [&](const tbb::blocked_range<size_t> &r) {
                        for (size_t i = r.begin(); i != r.end(); ++i) {
                          frantic::graphics::vector3 f = outMethod.get_face(i);
                          size_t idx = i * 3;
                          faces_ptr[idx + 0] = (int)f.x;
                          faces_ptr[idx + 1] = (int)f.y;
                          faces_ptr[idx + 2] = (int)f.z;
                        }
                      });
  }

  return std::make_tuple(vertices, faces);
}
