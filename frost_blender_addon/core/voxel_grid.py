"""
Voxel Grid Implementation for Frost Particle Meshing

This module provides voxel grid construction and sampling functionality
for implicit surface generation.
"""

import numpy as np
from typing import Tuple, Optional


class VoxelGrid:
    """
    A 3D voxel grid for storing and sampling scalar fields.
    """
    
    def __init__(self, bounds_min: np.ndarray, bounds_max: np.ndarray, voxel_size: float):
        """
        Initialize a voxel grid.
        
        Args:
            bounds_min: Minimum bounds (x, y, z)
            bounds_max: Maximum bounds (x, y, z)
            voxel_size: Size of each voxel
        """
        self.bounds_min = np.array(bounds_min, dtype=np.float32)
        self.bounds_max = np.array(bounds_max, dtype=np.float32)
        self.voxel_size = float(voxel_size)
        
        # Calculate grid dimensions
        grid_size = (self.bounds_max - self.bounds_min) / self.voxel_size
        self.resolution = np.ceil(grid_size).astype(np.int32) + 1
        
        # Initialize scalar field
        self.field = np.zeros(self.resolution, dtype=np.float32)
        
    def world_to_grid(self, world_pos: np.ndarray) -> np.ndarray:
        """Convert world coordinates to grid indices."""
        return ((world_pos - self.bounds_min) / self.voxel_size).astype(np.int32)
    
    def grid_to_world(self, grid_pos: np.ndarray) -> np.ndarray:
        """Convert grid indices to world coordinates."""
        return self.bounds_min + grid_pos * self.voxel_size
    
    def is_valid_index(self, idx: np.ndarray) -> bool:
        """Check if grid index is within bounds."""
        return np.all(idx >= 0) and np.all(idx < self.resolution)
    
    def set_value(self, idx: Tuple[int, int, int], value: float):
        """Set scalar value at grid index."""
        if self.is_valid_index(np.array(idx)):
            self.field[idx[0], idx[1], idx[2]] = value
    
    def get_value(self, idx: Tuple[int, int, int]) -> float:
        """Get scalar value at grid index."""
        if self.is_valid_index(np.array(idx)):
            return self.field[idx[0], idx[1], idx[2]]
        return 0.0
    
    def sample_trilinear(self, world_pos: np.ndarray) -> float:
        """
        Sample the grid using trilinear interpolation.
        
        Args:
            world_pos: World position (x, y, z)
            
        Returns:
            Interpolated scalar value
        """
        # Convert to grid coordinates (float)
        grid_pos = (world_pos - self.bounds_min) / self.voxel_size
        
        # Get integer and fractional parts
        idx0 = np.floor(grid_pos).astype(np.int32)
        t = grid_pos - idx0
        
        # Clamp to valid range
        idx0 = np.clip(idx0, 0, self.resolution - 2)
        idx1 = idx0 + 1
        
        # Trilinear interpolation
        c000 = self.field[idx0[0], idx0[1], idx0[2]]
        c001 = self.field[idx0[0], idx0[1], idx1[2]]
        c010 = self.field[idx0[0], idx1[1], idx0[2]]
        c011 = self.field[idx0[0], idx1[1], idx1[2]]
        c100 = self.field[idx1[0], idx0[1], idx0[2]]
        c101 = self.field[idx1[0], idx0[1], idx1[2]]
        c110 = self.field[idx1[0], idx1[1], idx0[2]]
        c111 = self.field[idx1[0], idx1[1], idx1[2]]
        
        # Interpolate along x
        c00 = c000 * (1 - t[0]) + c100 * t[0]
        c01 = c001 * (1 - t[0]) + c101 * t[0]
        c10 = c010 * (1 - t[0]) + c110 * t[0]
        c11 = c011 * (1 - t[0]) + c111 * t[0]
        
        # Interpolate along y
        c0 = c00 * (1 - t[1]) + c10 * t[1]
        c1 = c01 * (1 - t[1]) + c11 * t[1]
        
        # Interpolate along z
        return c0 * (1 - t[2]) + c1 * t[2]


class SpatialHash:
    """
    Spatial hash grid for fast particle nearest-neighbor queries.
    """
    
    def __init__(self, cell_size: float):
        """
        Initialize spatial hash.
        
        Args:
            cell_size: Size of each hash cell
        """
        self.cell_size = cell_size
        self.hash_table = {}
        
    def hash_position(self, pos: np.ndarray) -> Tuple[int, int, int]:
        """Convert position to hash cell index."""
        return tuple((pos / self.cell_size).astype(np.int32))
    
    def insert(self, particle_idx: int, position: np.ndarray):
        """Insert a particle into the hash table."""
        cell = self.hash_position(position)
        if cell not in self.hash_table:
            self.hash_table[cell] = []
        self.hash_table[cell].append(particle_idx)
    
    def query_radius(self, position: np.ndarray, radius: float, 
                     all_positions: np.ndarray) -> np.ndarray:
        """
        Query all particles within radius of position.
        
        Args:
            position: Query position
            radius: Search radius
            all_positions: Array of all particle positions (N, 3)
            
        Returns:
            Array of particle indices within radius
        """
        # Calculate search range in cells
        search_cells = int(np.ceil(radius / self.cell_size))
        center_cell = self.hash_position(position)
        
        candidates = []
        
        # Check neighboring cells
        for dx in range(-search_cells, search_cells + 1):
            for dy in range(-search_cells, search_cells + 1):
                for dz in range(-search_cells, search_cells + 1):
                    cell = (center_cell[0] + dx, center_cell[1] + dy, center_cell[2] + dz)
                    if cell in self.hash_table:
                        candidates.extend(self.hash_table[cell])
        
        if not candidates:
            return np.array([], dtype=np.int32)
        
        # Filter by actual distance
        candidates = np.array(candidates, dtype=np.int32)
        distances = np.linalg.norm(all_positions[candidates] - position, axis=1)
        return candidates[distances <= radius]
