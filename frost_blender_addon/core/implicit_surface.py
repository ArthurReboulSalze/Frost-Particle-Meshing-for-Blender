"""
Implicit Surface and Marching Cubes Implementation

This module provides marching cubes algorithm for extracting isosurfaces
from scalar fields.
"""

import numpy as np
from typing import Tuple, List


# Marching Cubes edge table (256 cases, 12 edges per cube)
# Each entry is a bitfield indicating which edges contain vertices
EDGE_TABLE = np.array([
    0x0, 0x109, 0x203, 0x30a, 0x406, 0x50f, 0x605, 0x70c,
    0x80c, 0x905, 0xa0f, 0xb06, 0xc0a, 0xd03, 0xe09, 0xf00,
    0x190, 0x99, 0x393, 0x29a, 0x596, 0x49f, 0x795, 0x69c,
    0x99c, 0x895, 0xb9f, 0xa96, 0xd9a, 0xc93, 0xf99, 0xe90,
    0x230, 0x339, 0x33, 0x13a, 0x636, 0x73f, 0x435, 0x53c,
    0xa3c, 0xb35, 0x83f, 0x936, 0xe3a, 0xf33, 0xc39, 0xd30,
    0x3a0, 0x2a9, 0x1a3, 0xaa, 0x7a6, 0x6af, 0x5a5, 0x4ac,
    0xbac, 0xaa5, 0x9af, 0x8a6, 0xfaa, 0xea3, 0xda9, 0xca0,
    0x460, 0x569, 0x663, 0x76a, 0x66, 0x16f, 0x265, 0x36c,
    0xc6c, 0xd65, 0xe6f, 0xf66, 0x86a, 0x963, 0xa69, 0xb60,
    0x5f0, 0x4f9, 0x7f3, 0x6fa, 0x1f6, 0xff, 0x3f5, 0x2fc,
    0xdfc, 0xcf5, 0xfff, 0xef6, 0x9fa, 0x8f3, 0xbf9, 0xaf0,
    0x650, 0x759, 0x453, 0x55a, 0x256, 0x35f, 0x55, 0x15c,
    0xe5c, 0xf55, 0xc5f, 0xd56, 0xa5a, 0xb53, 0x859, 0x950,
    0x7c0, 0x6c9, 0x5c3, 0x4ca, 0x3c6, 0x2cf, 0x1c5, 0xcc,
    0xfcc, 0xec5, 0xdcf, 0xcc6, 0xbca, 0xac3, 0x9c9, 0x8c0,
    0x8c0, 0x9c9, 0xac3, 0xbca, 0xcc6, 0xdcf, 0xec5, 0xfcc,
    0xcc, 0x1c5, 0x2cf, 0x3c6, 0x4ca, 0x5c3, 0x6c9, 0x7c0,
    0x950, 0x859, 0xb53, 0xa5a, 0xd56, 0xc5f, 0xf55, 0xe5c,
    0x15c, 0x55, 0x35f, 0x256, 0x55a, 0x453, 0x759, 0x650,
    0xaf0, 0xbf9, 0x8f3, 0x9fa, 0xef6, 0xfff, 0xcf5, 0xdfc,
    0x2fc, 0x3f5, 0xff, 0x1f6, 0x6fa, 0x7f3, 0x4f9, 0x5f0,
    0xb60, 0xa69, 0x963, 0x86a, 0xf66, 0xe6f, 0xd65, 0xc6c,
    0x36c, 0x265, 0x16f, 0x66, 0x76a, 0x663, 0x569, 0x460,
    0xca0, 0xda9, 0xea3, 0xfaa, 0x8a6, 0x9af, 0xaa5, 0xbac,
    0x4ac, 0x5a5, 0x6af, 0x7a6, 0xaa, 0x1a3, 0x2a9, 0x3a0,
    0xd30, 0xc39, 0xf33, 0xe3a, 0x936, 0x83f, 0xb35, 0xa3c,
    0x53c, 0x435, 0x73f, 0x636, 0x13a, 0x33, 0x339, 0x230,
    0xe90, 0xf99, 0xc93, 0xd9a, 0xa96, 0xb9f, 0x895, 0x99c,
    0x69c, 0x795, 0x49f, 0x596, 0x29a, 0x393, 0x99, 0x190,
    0xf00, 0xe09, 0xd03, 0xc0a, 0xb06, 0xa0f, 0x905, 0x80c,
    0x70c, 0x605, 0x50f, 0x406, 0x30a, 0x203, 0x109, 0x0
], dtype=np.int16)


# Triangle table defines which edges form triangles for each case
# This is a simplified version - full implementation would have 256 entries
# For now, we'll use a basic implementation

def marching_cubes(scalar_field: np.ndarray, isovalue: float = 0.0, 
                   voxel_size: float = 1.0, bounds_min: np.ndarray = None) -> Tuple[np.ndarray, np.ndarray]:
    """
    Extract isosurface from scalar field using marching cubes algorithm.
    
    Args:
        scalar_field: 3D array of scalar values
        isovalue: Threshold value for surface
        voxel_size: Size of each voxel
        bounds_min: Minimum bounds of the grid
        
    Returns:
        vertices: Array of vertex positions (N, 3)
        faces: Array of triangle indices (M, 3)
    """
    if bounds_min is None:
        bounds_min = np.zeros(3)
    
    vertices = []
    faces = []
    vertex_cache = {}
    
    # Iterate through each cube in the grid
    nx, ny, nz = scalar_field.shape
    for x in range(nx - 1):
        for y in range(ny - 1):
            for z in range(nz - 1):
                # Get the 8 corner values
                cube_values = np.array([
                    scalar_field[x, y, z],
                    scalar_field[x + 1, y, z],
                    scalar_field[x + 1, y, z + 1],
                    scalar_field[x, y, z + 1],
                    scalar_field[x, y + 1, z],
                    scalar_field[x + 1, y + 1, z],
                    scalar_field[x + 1, y + 1, z + 1],
                    scalar_field[x, y + 1, z + 1]
                ])
                
                # Calculate cube index
                cube_index = 0
                for i in range(8):
                    if cube_values[i] < isovalue:
                        cube_index |= (1 << i)
                
                # Skip if completely inside or outside
                if cube_index == 0 or cube_index == 255:
                    continue
                
                # Process edges and create triangles
                process_cube(x, y, z, cube_values, cube_index, isovalue,
                           voxel_size, bounds_min, vertices, faces, vertex_cache)
    
    if not vertices:
        return np.zeros((0, 3), dtype=np.float32), np.zeros((0, 3), dtype=np.int32)
    
    return np.array(vertices, dtype=np.float32), np.array(faces, dtype=np.int32)


def process_cube(x: int, y: int, z: int, cube_values: np.ndarray, cube_index: int,
                isovalue: float, voxel_size: float, bounds_min: np.ndarray,
                vertices: List, faces: List, vertex_cache: dict):
    """
    Process a single cube for marching cubes.
    """
    # Edge vertex positions (relative to cube corner)
    edge_vertices = [
        [[0, 0, 0], [1, 0, 0]],  # Edge 0
        [[1, 0, 0], [1, 0, 1]],  # Edge 1
        [[1, 0, 1], [0, 0, 1]],  # Edge 2
        [[0, 0, 1], [0, 0, 0]],  # Edge 3
        [[0, 1, 0], [1, 1, 0]],  # Edge 4
        [[1, 1, 0], [1, 1, 1]],  # Edge 5
        [[1, 1, 1], [0, 1, 1]],  # Edge 6
        [[0, 1, 1], [0, 1, 0]],  # Edge 7
        [[0, 0, 0], [0, 1, 0]],  # Edge 8
        [[1, 0, 0], [1, 1, 0]],  # Edge 9
        [[1, 0, 1], [1, 1, 1]],  # Edge 10
        [[0, 0, 1], [0, 1, 1]]   # Edge 11
    ]
    
    # Vertex indices for each edge endpoint
    edge_vertex_indices = [
        [0, 1], [1, 2], [2, 3], [3, 0],
        [4, 5], [5, 6], [6, 7], [7, 4],
        [0, 4], [1, 5], [2, 6], [3, 7]
    ]
    
    # Create vertices on edges by interpolation
    edge_to_vertex = {}
    edge_flags = EDGE_TABLE[cube_index]
    
    for edge_idx in range(12):
        if edge_flags & (1 << edge_idx):
            # Interpolate vertex position on this edge
            v0_idx, v1_idx = edge_vertex_indices[edge_idx]
            val0, val1 = cube_values[v0_idx], cube_values[v1_idx]
            
            # Linear interpolation
            t = (isovalue - val0) / (val1 - val0 + 1e-10)
            t = np.clip(t, 0.0, 1.0)
            
            # Calculate world position
            edge_start = np.array(edge_vertices[edge_idx][0])
            edge_end = np.array(edge_vertices[edge_idx][1])
            local_pos = edge_start + t * (edge_end - edge_start)
            world_pos = bounds_min + (np.array([x, y, z]) + local_pos) * voxel_size
            
            # Cache vertex
            vertex_key = (x, y, z, edge_idx)
            if vertex_key not in vertex_cache:
                vertex_cache[vertex_key] = len(vertices)
                vertices.append(world_pos)
            
            edge_to_vertex[edge_idx] = vertex_cache[vertex_key]
    
    # Create triangles (simplified - using basic triangulation)
    # A full implementation would use triangle tables
    if len(edge_to_vertex) >= 3:
        edge_indices = list(edge_to_vertex.values())
        # Create simple fan triangulation
        for i in range(1, len(edge_indices) - 1):
            faces.append([edge_indices[0], edge_indices[i], edge_indices[i + 1]])


def generate_implicit_field_metaball(positions: np.ndarray, radii: np.ndarray,
                                     voxel_grid) -> np.ndarray:
    """
    Generate metaball implicit field in voxel grid.
    
    Args:
        positions: Particle positions (N, 3)
        radii: Particle radii (N,)
        voxel_grid: VoxelGrid object
        
    Returns:
        Scalar field array
    """
    from .voxel_grid import SpatialHash
    
    # Build spatial hash for efficient queries
    max_radius = np.max(radii)
    spatial_hash = SpatialHash(max_radius * 2)
    
    for i, pos in enumerate(positions):
        spatial_hash.insert(i, pos)
    
    # Generate field values
    field = np.zeros(voxel_grid.resolution, dtype=np.float32)
    
    for ix in range(voxel_grid.resolution[0]):
        for iy in range(voxel_grid.resolution[1]):
            for iz in range(voxel_grid.resolution[2]):
                # Get world position of voxel center
                world_pos = voxel_grid.grid_to_world(np.array([ix, iy, iz]))
                
                # Query nearby particles
                nearby_particles = spatial_hash.query_radius(
                    world_pos, max_radius * 3, positions
                )
                
                # Sum metaball contributions
                value = 0.0
                for p_idx in nearby_particles:
                    p_pos = positions[p_idx]
                    p_radius = radii[p_idx]
                    
                    dist = np.linalg.norm(world_pos - p_pos)
                    if dist < p_radius * 2:
                        # Metaball kernel: f(r) = 1 - (r/R)^2
                        normalized_dist = dist / (p_radius * 2)
                        value += max(0, 1 - normalized_dist ** 2)
                
                field[ix, iy, iz] = value
    
    return field
