"""
Particle Extractor for Blender

Extracts particle data from various Blender sources:
- Particle Systems
- Geometry Nodes (Point Cloud)
- Mesh vertices
"""

import bpy
import numpy as np
from typing import Tuple, Optional


def _get_evaluated_object(obj: bpy.types.Object) -> bpy.types.Object:
    """Return the depsgraph-evaluated Blender object."""
    depsgraph = bpy.context.evaluated_depsgraph_get()
    return obj.evaluated_get(depsgraph)


def _transform_positions_to_world(positions: np.ndarray, matrix_world) -> np.ndarray:
    """Transform an (N, 3) position array from local to world space."""
    if len(positions) == 0:
        return positions.astype(np.float32, copy=False)

    matrix = np.array(matrix_world, dtype=np.float32)
    rotation_scale = matrix[:3, :3]
    translation = matrix[:3, 3]
    world_positions = positions @ rotation_scale.T
    world_positions += translation
    return world_positions.astype(np.float32, copy=False)


def extract_from_particle_system(obj: bpy.types.Object, 
                                 particle_system_index: int = 0) -> Tuple[np.ndarray, np.ndarray, Optional[np.ndarray]]:
    """
    Extract particles from a Blender Particle System.
    """
    # Get evaluated object to ensure we get current particle state
    eval_obj = _get_evaluated_object(obj)
    
    if not eval_obj.particle_systems:
        return np.zeros((0, 3)), np.zeros(0), None
    
    if particle_system_index >= len(eval_obj.particle_systems):
        particle_system_index = 0
    
    ps = eval_obj.particle_systems[particle_system_index]
    particles = ps.particles
    
    num_particles = len(particles)
    if num_particles == 0:
        return np.zeros((0, 3)), np.zeros(0), None
    
    # Extract positions
    positions = np.zeros((num_particles, 3), dtype=np.float32)
    radii = np.zeros(num_particles, dtype=np.float32)
    velocities = np.zeros((num_particles, 3), dtype=np.float32)
    
    # Optimization: Use foreach_get for faster extraction if possible, 
    # but particle.location is a float vector, require flattening.
    # For now, stick to loop for safety/clarity, or use collection property access
    
    # Fast path using foreach_get
    locations_flat = np.zeros(num_particles * 3, dtype=np.float32)
    velocities_flat = np.zeros(num_particles * 3, dtype=np.float32)
    sizes = np.zeros(num_particles, dtype=np.float32)
    alive_states = np.zeros(num_particles, dtype=np.int32)
    birth_times = np.zeros(num_particles, dtype=np.float32)
    
    particles.foreach_get("location", locations_flat)
    particles.foreach_get("velocity", velocities_flat)
    particles.foreach_get("size", sizes)
    particles.foreach_get("alive_state", alive_states)
    particles.foreach_get("birth_time", birth_times)
    
    # Reshape
    positions = locations_flat.reshape((num_particles, 3))
    velocities = velocities_flat.reshape((num_particles, 3))
    radii = sizes
    
    # Filter by Alive State
    # 0: DEAD, 1: UNBORN, 2: ALIVE, 3: DYING
    # We generally want ALIVE (2) and DYING (3). 
    # UNBORN (1) are particles waiting to be emitted (often sitting on emitter).
    # DEAD (0) are particles that have disappeared.
    
    # Also filter out very young particles (just born this frame)
    # They are at emitter surface and create artifacts.
    current_frame = bpy.context.scene.frame_current
    min_age = 0.5  # Half a frame minimum age
    particle_age = current_frame - birth_times
    
    # Create mask: (state == 2 or 3) AND (age > min_age)
    valid_mask = ((alive_states == 2) | (alive_states == 3)) & (particle_age > min_age)
    
    # Apply filter
    positions = positions[valid_mask]
    velocities = velocities[valid_mask]
    radii = radii[valid_mask]
    
    if len(positions) == 0:
         return np.zeros((0, 3)), np.zeros(0), None


    
    # NOTE: particle.location is ALREADY in World Space for evaluated systems.
    # So we do NOT need to multiply by matrix_world again.
    # Doing so causes double-transformation (flying particles).
    
    return positions.astype(np.float32), radii.astype(np.float32), velocities.astype(np.float32)





def extract_from_point_cloud(obj: bpy.types.Object) -> Tuple[np.ndarray, np.ndarray, Optional[np.ndarray]]:
    """
    Extract particles from a Point Cloud object (Geometry Nodes).
    
    Args:
        obj: Blender Point Cloud object
        
    Returns:
        positions: (N, 3) array of positions
        radii: (N,) array of radii (uniform)
        velocities: None (not supported for point clouds yet)
    """
    if obj.type != 'POINTCLOUD':
        return np.zeros((0, 3)), np.zeros(0), None

    eval_obj = _get_evaluated_object(obj)
    point_cloud = eval_obj.data
    num_points = len(point_cloud.points)
    
    if num_points == 0:
        return np.zeros((0, 3)), np.zeros(0), None
    
    positions_flat = np.zeros(num_points * 3, dtype=np.float32)
    point_cloud.points.foreach_get("co", positions_flat)
    positions = positions_flat.reshape((num_points, 3))
    positions = _transform_positions_to_world(positions, eval_obj.matrix_world)
    
    # Check for radius attribute
    radii = np.ones(num_points, dtype=np.float32) * 0.1  # Default radius
    
    if 'radius' in point_cloud.attributes:
        radius_attr = point_cloud.attributes['radius']
        radii_data = np.zeros(num_points, dtype=np.float32)
        radius_attr.data.foreach_get('value', radii_data)
        radii = radii_data
    
    return positions, radii, None


def extract_from_mesh_vertices(obj: bpy.types.Object, 
                               default_radius: float = 0.1) -> Tuple[np.ndarray, np.ndarray, Optional[np.ndarray]]:
    """
    Extract particles from mesh vertices.
    
    Args:
        obj: Blender Mesh object
        default_radius: Default radius for each vertex
        
    Returns:
        positions: (N, 3) array of positions
        radii: (N,) array of radii (uniform)
        velocities: None
    """
    if obj.type != 'MESH':
        return np.zeros((0, 3)), np.zeros(0), None

    eval_obj = _get_evaluated_object(obj)
    eval_mesh = eval_obj.to_mesh()
    if eval_mesh is None:
        return np.zeros((0, 3)), np.zeros(0), None

    try:
        num_verts = len(eval_mesh.vertices)

        if num_verts == 0:
            return np.zeros((0, 3)), np.zeros(0), None

        coords_flat = np.zeros(num_verts * 3, dtype=np.float32)
        eval_mesh.vertices.foreach_get("co", coords_flat)
        positions = coords_flat.reshape((num_verts, 3))
        positions = _transform_positions_to_world(positions, eval_obj.matrix_world)

        radii = np.full(num_verts, default_radius, dtype=np.float32)
        return positions, radii, None
    finally:
        eval_obj.to_mesh_clear()


def extract_particles(obj: bpy.types.Object, 
                      source_type: str = 'AUTO',
                      particle_system_index: int = 0,
                      default_radius: float = 0.1) -> Tuple[np.ndarray, np.ndarray, Optional[np.ndarray]]:
    """
    Extract particles from a Blender object (auto-detect source type).
    
    Args:
        obj: Blender object
        source_type: 'AUTO', 'PARTICLE_SYSTEM', 'POINT_CLOUD', or 'MESH'
        particle_system_index: Index of particle system if using particle systems
        default_radius: Default radius for mesh vertices
        
    Returns:
        positions: (N, 3) array of positions
        radii: (N,) array of radii
        velocities: (N, 3) array of velocities or None
    """
    if source_type == 'AUTO':
        # Auto-detect source type
        if obj.particle_systems:
            source_type = 'PARTICLE_SYSTEM'
        elif obj.type == 'POINTCLOUD':
            source_type = 'POINT_CLOUD'
        elif obj.type == 'MESH':
            source_type = 'MESH'
        else:
            return np.zeros((0, 3)), np.zeros(0), None
    
    if source_type == 'PARTICLE_SYSTEM':
        return extract_from_particle_system(obj, particle_system_index)
    elif source_type == 'POINT_CLOUD':
        return extract_from_point_cloud(obj)
    elif source_type == 'MESH':
        return extract_from_mesh_vertices(obj, default_radius)
    else:
        return np.zeros((0, 3)), np.zeros(0), None
