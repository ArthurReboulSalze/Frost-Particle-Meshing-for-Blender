import os
import sys
import ctypes

# Add current directory to DLL search path
if hasattr(os, "add_dll_directory"):
    try:
        os.add_dll_directory(os.getcwd())
        print("Added current directory to DLL search path")
    except Exception as e:
        print(f"Failed to add DLL dir: {e}")

# Try loading dependencies explicitly to see which one fails
dlls = [
    "tbb12.dll",
    "Imath-3_2.dll",
    "OpenEXR-3_4.dll",
    "boost_thread-vc143-mt-x64-1_90.dll" 
    # Add others if needed
]

for dll_name in dlls:
    dll_path = os.path.abspath(dll_name)
    print(f"Testing load of: {dll_name}")
    try:
        ctypes.CDLL(dll_path)
        print(f"  SUCCESS: {dll_name} loaded.")
    except Exception as e:
        print(f"  FAIL: {dll_name} failed to load. Error: {e}")

print("\nAttempting to import blender_frost_adapter...")
try:
    import blender_frost_adapter
    print("SUCCESS: blender_frost_adapter imported successfully!")

    print("\n--- Testing FrostInterface ---")
    frost = blender_frost_adapter.FrostInterface()
    
    print("Setting Relax Iterations to 10...")
    frost.set_parameter("relax_iterations", 10)
    print("Setting Relax Strength to 0.5...")
    frost.set_parameter("relax_strength", 0.5)
    print("Setting Push Distance to 0.1...")
    frost.set_parameter("push_distance", 0.1)

    
    # Fake particles
    import numpy as np
    pos = np.array([[0,0,0], [1,0,0]], dtype=np.float32)
    rad = np.array([0.5, 0.5], dtype=np.float32)
    vel = None
    frost.set_particles(pos, rad, vel)

    print("Generating Mesh...")
    # This should trigger the [FROST_DEBUG] prints
    verts, faces = frost.generate_mesh()
    print(f"Generated {len(verts)} verts")

except ImportError as e:
    print(f"FAIL: ImportError: {e}")
except Exception as e:
    print(f"FAIL: Unexpected error: {e}")
    import traceback
    traceback.print_exc()
