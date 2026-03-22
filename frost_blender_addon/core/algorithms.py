"""
Core Meshing Algorithms for Frost

Implements different particle meshing strategies:
- Union of Spheres
- Metaball
- Zhu-Bridson
"""

import numpy as np
from typing import Tuple, Optional
from .voxel_grid import VoxelGrid, SpatialHash
from .implicit_surface import marching_cubes, generate_implicit_field_metaball


class BaseMesher:
    """Base class for all meshers."""
    
    def __init__(self, voxel_size: float = 0.1):
        self.voxel_size = voxel_size
    
    def mesh(self, positions: np.ndarray, radii: np.ndarray, 
             **kwargs) -> Tuple[np.ndarray, np.ndarray]:
        """
        Generate mesh from particles.
        
        Args:
            positions: Particle positions (N, 3)
            radii: Particle radii (N,)
            
        Returns:
            vertices: Mesh vertices (V, 3)
            faces: Mesh faces (F, 3)
        """
        raise NotImplementedError


class UnionOfSpheresMesher(BaseMesher):
    """
    Simple union of spheres meshing.
    Creates a sphere mesh for each particle and combines them.
    """
    
    def __init__(self, voxel_size: float = 0.1, sphere_subdivisions: int = 2):
        super().__init__(voxel_size)
        self.sphere_subdivisions = sphere_subdivisions
        self._sphere_template = self._create_ico_sphere(sphere_subdivisions)
    
    def _create_ico_sphere(self, subdivisions: int) -> Tuple[np.ndarray, np.ndarray]:
        """Create an icosphere template."""
        # Golden ratio
        phi = (1.0 + np.sqrt(5.0)) / 2.0
        
        # Icosahedron vertices
        vertices = np.array([
            [-1, phi, 0], [1, phi, 0], [-1, -phi, 0], [1, -phi, 0],
            [0, -1, phi], [0, 1, phi], [0, -1, -phi], [0, 1, -phi],
            [phi, 0, -1], [phi, 0, 1], [-phi, 0, -1], [-phi, 0, 1]
        ], dtype=np.float32)
        
        # Normalize
        vertices /= np.linalg.norm(vertices[0])
        
        # Icosahedron faces
        faces = np.array([
            [0, 11, 5], [0, 5, 1], [0, 1, 7], [0, 7, 10], [0, 10, 11],
            [1, 5, 9], [5, 11, 4], [11, 10, 2], [10, 7, 6], [7, 1, 8],
            [3, 9, 4], [3, 4, 2], [3, 2, 6], [3, 6, 8], [3, 8, 9],
            [4, 9, 5], [2, 4, 11], [6, 2, 10], [8, 6, 7], [9, 8, 1]
        ], dtype=np.int32)
        
        # Subdivide if needed
        for _ in range(subdivisions):
            vertices, faces = self._subdivide_mesh(vertices, faces)
        
        return vertices, faces
    
    def _subdivide_mesh(self, vertices: np.ndarray, faces: np.ndarray) -> Tuple[np.ndarray, np.ndarray]:
        """Subdivide triangle mesh."""
        new_vertices = list(vertices)
        new_faces = []
        edge_cache = {}
        
        def get_midpoint(v0_idx: int, v1_idx: int) -> int:
            edge = tuple(sorted([v0_idx, v1_idx]))
            if edge in edge_cache:
                return edge_cache[edge]
            
            v0 = vertices[v0_idx]
            v1 = vertices[v1_idx]
            mid = (v0 + v1) / 2.0
            mid /= np.linalg.norm(mid)  # Project to sphere
            
            idx = len(new_vertices)
            new_vertices.append(mid)
            edge_cache[edge] = idx
            return idx
        
        for face in faces:
            v0, v1, v2 = face
            
            # Get midpoints
            a = get_midpoint(v0, v1)
            b = get_midpoint(v1, v2)
            c = get_midpoint(v2, v0)
            
            # Create 4 new faces
            new_faces.extend([[v0, a, c], [v1, b, a], [v2, c, b], [a, b, c]])
        
        return np.array(new_vertices, dtype=np.float32), np.array(new_faces, dtype=np.int32)
    
    def mesh(self, positions: np.ndarray, radii: np.ndarray, 
             **kwargs) -> Tuple[np.ndarray, np.ndarray]:
        """Generate mesh as union of spheres."""
        if len(positions) == 0:
            return np.zeros((0, 3), dtype=np.float32), np.zeros((0, 3), dtype=np.int32)
        
        all_vertices = []
        all_faces = []
        vertex_offset = 0
        
        sphere_verts, sphere_faces = self._sphere_template
        
        for i, (pos, radius) in enumerate(zip(positions, radii)):
            # Scale and translate sphere
            verts = sphere_verts * radius + pos
            faces = sphere_faces + vertex_offset
            
            all_vertices.append(verts)
            all_faces.append(faces)
            vertex_offset += len(verts)
        
        if not all_vertices:
            return np.zeros((0, 3), dtype=np.float32), np.zeros((0, 3), dtype=np.int32)
        
        vertices = np.vstack(all_vertices)
        faces = np.vstack(all_faces)
        
        return vertices, faces


class MetaballMesher(BaseMesher):
    """
    Metaball/blobby surface meshing using implicit surfaces.
    """
    
    def __init__(self, voxel_size: float = 0.1, isovalue: float = 0.5):
        super().__init__(voxel_size)
        self.isovalue = isovalue
    
    def mesh(self, positions: np.ndarray, radii: np.ndarray, 
             **kwargs) -> Tuple[np.ndarray, np.ndarray]:
        """Generate mesh using metaball implicit surface."""
        if len(positions) == 0:
            return np.zeros((0, 3), dtype=np.float32), np.zeros((0, 3), dtype=np.int32)
        
        # Calculate bounds with padding
        max_radius = np.max(radii)
        bounds_min = np.min(positions, axis=0) - max_radius * 3
        bounds_max = np.max(positions, axis=0) + max_radius * 3
        
        # Create voxel grid
        voxel_grid = VoxelGrid(bounds_min, bounds_max, self.voxel_size)
        
        # Generate implicit field
        field = generate_implicit_field_metaball(positions, radii, voxel_grid)
        
        # Extract isosurface using marching cubes
        vertices, faces = marching_cubes(field, self.isovalue, self.voxel_size, bounds_min)
        
        return vertices, faces


class ZhuBridsonMesher(BaseMesher):
    """
    Zhu-Bridson particle meshing for fluids.
    Uses anisotropic kernels for better fluid surfaces.
    """
    
    def __init__(self, voxel_size: float = 0.1, blend_radius_scale: float = 2.0):
        super().__init__(voxel_size)
        self.blend_radius_scale = blend_radius_scale
    
    def mesh(self, positions: np.ndarray, radii: np.ndarray, 
             velocities: Optional[np.ndarray] = None, 
             **kwargs) -> Tuple[np.ndarray, np.ndarray]:
        """
        Generate mesh using Zhu-Bridson method.
        
        Args:
            positions: Particle positions
            radii: Particle radii
            velocities: Optional particle velocities for anisotropy
        """
        if len(positions) == 0:
            return np.zeros((0, 3), dtype=np.float32), np.zeros((0, 3), dtype=np.int32)
        
        # For now, use metaball-like approach
        # Full Zhu-Bridson would require anisotropic kernels
        max_radius = np.max(radii) * self.blend_radius_scale
        bounds_min = np.min(positions, axis=0) - max_radius * 2
        bounds_max = np.max(positions, axis=0) + max_radius * 2
        
        voxel_grid = VoxelGrid(bounds_min, bounds_max, self.voxel_size)
        
        # Generate field with larger blend radius
        scaled_radii = radii * self.blend_radius_scale
        field = self._generate_zhu_bridson_field(positions, scaled_radii, voxel_grid)
        
        vertices, faces = marching_cubes(field, 0.5, self.voxel_size, bounds_min)
        
        return vertices, faces
    
    def _generate_zhu_bridson_field(self, positions: np.ndarray, radii: np.ndarray,
                                    voxel_grid: VoxelGrid) -> np.ndarray:
        """Generate Zhu-Bridson implicit field."""
        from .implicit_surface import generate_implicit_field_metaball
        # Use metaball field for now - could be extended with proper ZB kernel
        return generate_implicit_field_metaball(positions, radii, voxel_grid)


class AnisotropicMesher(BaseMesher):
    """
    Anisotropic meshing based on particle velocity/orientation.
    Creates stretched metaballs aligned with particle motion.
    """
    
    def __init__(self, voxel_size: float = 0.1, max_anisotropy: float = 3.0):
        super().__init__(voxel_size)
        self.max_anisotropy = max_anisotropy
    
    def mesh(self, positions: np.ndarray, radii: np.ndarray,
             velocities: Optional[np.ndarray] = None,
             **kwargs) -> Tuple[np.ndarray, np.ndarray]:
        """Generate anisotropic mesh."""
        if len(positions) == 0:
            return np.zeros((0, 3), dtype=np.float32), np.zeros((0, 3), dtype=np.int32)
        
        # Fallback to metaball if no velocities
        if velocities is None:
            metaball_mesher = MetaballMesher(self.voxel_size)
            return metaball_mesher.mesh(positions, radii)
        
        # TODO: Implement proper anisotropic kernels
        # For now, use metaball as placeholder
        metaball_mesher = MetaballMesher(self.voxel_size)
        return metaball_mesher.mesh(positions, radii)
