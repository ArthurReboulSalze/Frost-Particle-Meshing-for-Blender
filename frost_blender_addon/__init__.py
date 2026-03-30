"""
Frost - Particle Meshing for Blender

A Blender addon for converting particles into meshes using various algorithms
inspired by Thinkbox Frost.

Supported Methods:
- Union of Spheres: Fast preview meshing
- Metaball: Smooth blobby surfaces
- Zhu-Bridson: Advanced fluid meshing

Author: Arthur Reboul Salze
Version: 1.26.1
Blender: 5+
"""

bl_info = {
    "name": "Frost Particle Meshing",
    "author": "Arthur Reboul Salze",
    "version": (1, 26, 1),
    "blender": (5, 0, 0),
    "location": "View3D > Sidebar > Frost",
    "description": "Particle meshing plugin with Thinkbox CPU and the current Vulkan GPU backend work.",
    "warning": "",
    "doc_url": "https://github.com/ArthurReboulSalze/Frost-for-Blender",
    "category": "Mesh",
}


import bpy
from bpy.app.handlers import persistent

# Import addon modules
from . import operator
from . import ui


def _iter_frost_source_objects(props):
    """Yield enabled source objects referenced by a Frost object."""
    if not props:
        return

    if props.source_object:
        yield props.source_object

    for item in props.sources:
        if item.enabled and item.object:
            yield item.object


def _frost_has_sources(props):
    return bool(props and (props.source_object or len(props.sources) > 0))


@persistent
def frost_frame_change_handler(scene):
    """Update Frost meshes on frame change"""
    # Avoid errors if C++ not loaded or module unloaded
    if operator is None or not operator.blender_frost_adapter:
        return

    # Iterate over objects in the current scene
    try:
        for obj in scene.objects:
            if obj.type == 'MESH':
                # Check if it's a configured Frost object
                props = getattr(obj, "frost_properties", None)
                
                # Check for Main Source OR Additional Sources
                has_source = _frost_has_sources(props)
                
                if has_source and props.auto_update:
                    try:
                        operator.request_frost_update(obj, source="frame")
                    except Exception as e:
                        print(f"Frost Animation Error on {obj.name}: {e}")
    except ReferenceError:
        pass # Object already deleted during shutdown
    except Exception:
        pass # General shutdown noise


# Visibility Detection Handler
# Tracks visibility state to trigger update on Rising Edge (Hidden -> Visible)
_visibility_cache = {}

@persistent
def frost_visibility_handler(scene, depsgraph):
    """
    Check for source-driven depsgraph changes and visibility transitions.
    Triggers an immediate mesh update if Auto Update is enabled.
    """
    # Avoid errors if operator/adapter not loaded
    if operator is None or not operator.blender_frost_adapter:
        return

    try:
        updated_objects = set()
        updated_data_blocks = set()

        for update in depsgraph.updates:
            update_id = getattr(update, "id", None)
            if update_id is None:
                continue

            original_id = getattr(update_id, "original", update_id)
            if isinstance(original_id, bpy.types.Object):
                updated_objects.add(original_id)
                if getattr(update, "is_updated_geometry", False):
                    data = getattr(original_id, "data", None)
                    if data is not None:
                        updated_data_blocks.add(data)
            else:
                updated_data_blocks.add(original_id)

        # Check all objects in scene (robust) 
        # depsgraph updates might not contain all info for property changes
        for obj in scene.objects:
            if obj.type == 'MESH':
                # Quick check if it's a Frost object
                props = getattr(obj, "frost_properties", None)
                if not props: continue # Not a Frost object logic

                # Determine current state
                is_visible = not obj.hide_viewport
                
                # Retrieve previous state (Default True to avoid spike on initial load)
                was_visible = _visibility_cache.get(obj.name, True)
                
                # Logic: If becoming visible NOW, and was previously hidden
                # AND Auto Update is ON -> Trigger Update
                triggered_update = False
                if is_visible and not was_visible:
                    if props.auto_update:
                        try:
                            # We must re-check if source exists
                            if _frost_has_sources(props):
                                operator.request_frost_update(obj, source="visibility")
                                triggered_update = True
                        except Exception as e:
                            print(f"Frost Visibility Update Error: {e}")

                # Update cache
                _visibility_cache[obj.name] = is_visible

                if triggered_update or not props.auto_update or not _frost_has_sources(props):
                    continue

                source_changed = False
                for source_obj in _iter_frost_source_objects(props):
                    if source_obj == obj:
                        continue
                    if source_obj in updated_objects:
                        source_changed = True
                        break

                    source_data = getattr(source_obj, "data", None)
                    if source_data is not None and source_data in updated_data_blocks:
                        source_changed = True
                        break

                if source_changed:
                    try:
                        operator.request_frost_update(obj, source="depsgraph")
                    except Exception as e:
                        print(f"Frost Source Update Error on {obj.name}: {e}")
                
    except Exception:
        pass


# Registration
def register():
    """Register addon classes and properties."""
    ui.register()
    operator.register()
    
    if frost_frame_change_handler not in bpy.app.handlers.frame_change_post:
        bpy.app.handlers.frame_change_post.append(frost_frame_change_handler)

    if frost_visibility_handler not in bpy.app.handlers.depsgraph_update_post:
        bpy.app.handlers.depsgraph_update_post.append(frost_visibility_handler)
    
    print("Frost Particle Meshing addon registered")


def unregister():
    """Unregister addon classes and properties."""
    if frost_frame_change_handler in bpy.app.handlers.frame_change_post:
        bpy.app.handlers.frame_change_post.remove(frost_frame_change_handler)

    if frost_visibility_handler in bpy.app.handlers.depsgraph_update_post:
        bpy.app.handlers.depsgraph_update_post.remove(frost_visibility_handler)
        
    operator.unregister()
    ui.unregister()
    
    # Explicitly unload adapter to prevent TBB shutdown crashes
    if operator:
        operator.unload_adapter()
    
    print("Frost Particle Meshing addon unregistered")


if __name__ == "__main__":
    register()
