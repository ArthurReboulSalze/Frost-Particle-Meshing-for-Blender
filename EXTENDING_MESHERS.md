# Extending Frost: Adding New Meshing Algorithms

This document outlines the architectural requirements for adding new meshing strategies (e.g., Dual Contouring, Marching Cubes variants) to the Frost C++ core.

## 🏗 Architecture Overview

The Frost meshing engine relies on a **Polymorphic Strategy Pattern**. All meshing algorithms must inherit from the abstract base class `meshing_policy`.

### Key Files

1. **Interface Definition**: `thinkbox-frost/frost/meshing_policy.hpp`
    * Defines the `meshing_policy` abstract base class.
2. **Factory**: `thinkbox-frost/src/create_meshing_policy.cpp`
    * Instantiates the concrete policy based on the integer ID received from Blender/Python.
3. **Implementations**: `thinkbox-frost/src/meshing_policy.cpp`
    * Contains the concrete wrappers (e.g., `metaball_meshing_policy`) that bridge Frost parameters to the low-level `frantic` library.

---

## 🛠 Implementation Steps

To add a new mesher (e.g., "MyCustomMesher"):

### 1. Define the Class (C++)

Create a new class inheriting from `meshing_policy` in `meshing_policy.hpp` (or a header included there).

```cpp
class my_custom_meshing_policy : public meshing_policy {
public:
    // ... Constructor receiving parameters ...

    // Required overrides:
    virtual void build_mesh(...) override;
    virtual frost::sampler::ptr_type create_sampler(...) const override;
    // ... and others (see meshing_policy.hpp)
};
```

### 2. Implement the Logic (C++)

Implement the methods in `meshing_policy.cpp` (or a new source file).

* **`build_mesh`**: The main entry point. Typically calls a function from the `frantic` library or your own custom meshing code.
* **`create_sampler`**: Returns a field sampler for the implicit surface (used by the meshing algorithm to evaluate density/distance at various points).

### 3. Register in Factory (C++)

Update `create_meshing_policy.cpp` to include your new type in the switch statement.

```cpp
switch( meshingMethod ) {
    // ... existing cases ...
    case 4: // NEW ID
        return boost::make_shared<my_custom_meshing_policy>( /* params... */ );
}
```

### 4. Expose in Blender (Python)

Update `frost_blender_addon/ui.py` to add your new method to the `EnumProperty`.

```python
meshing_method: EnumProperty(
    items=[
        # ...
        ('4', "My Custom Mesher", "Description of the new algo"),
    ],
    # ...
)
```

### 5. Add Parameters (Optional)

If your mesher needs specific settings (like "Threshold" or "Smoothing Factor"):

1. Add properties to `ui.py`.
2. Update `FrostInterface::set_parameter` in `frost_interface.cpp` to forward these values to the C++ map.
3. Retrieve them in `create_meshing_policy.cpp` via `params.get_my_param()`.

---

## 💡 Notes on Dependencies

* **Frantic Library**: Frost relies heavily on the `frantic` library (included in `deps/thinkbox-library`) for the heavy mathematical lifting (Volumetrics, Level Sets, etc.). It is recommended to reuse existing Frantic tools where possible.
