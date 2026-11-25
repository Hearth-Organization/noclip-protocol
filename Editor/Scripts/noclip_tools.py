"""
Editor helpers for building Noclip Protocol levels (used through the Claude bridge, or from the editor Console).
    from noclip_tools import *
"""

import math as pymath
import os

import azlmbr
import azlmbr.asset as asset
import azlmbr.bus as bus
import azlmbr.components as components
import azlmbr.editor as editor
import azlmbr.entity as entity
import azlmbr.legacy.general as general
import azlmbr.math as azmath

def vec3(values):
    return azmath.Vector3(float(values[0]), float(values[1]), float(values[2]))


NON_UNIFORM_SCALE = azmath.Uuid_CreateString("{2933FB4F-B3DA-4CD1-8106-F37300730777}", 0)


def asset_id(path):
    return asset.AssetCatalogRequestBus(bus.Broadcast, "GetAssetIdByPath", path, azmath.Uuid(), False)


def material(name):
    return asset_id(f"assets/materials/level0/level0_{name}.azmaterial")


BOX_MODEL_PATH = "objects/_primitives/_box_1x1.fbx.azmodel"


def find_entity(name):
    search_filter = entity.SearchFilter()
    search_filter.names = [name]
    for entity_id in entity.SearchBus(bus.Broadcast, "SearchEntities", search_filter) or []:
        if entity_id.IsValid():
            return entity_id
    return None


def all_entities():
    search_filter = entity.SearchFilter()
    search_filter.names = ["*"]
    return list(entity.SearchBus(bus.Broadcast, "SearchEntities", search_filter) or [])


def name_of(entity_id):
    return editor.EditorEntityInfoRequestBus(bus.Event, "GetName", entity_id)


def create_entity(name, parent=None, position=(0.0, 0.0, 0.0), reuse=True):
    if reuse:
        existing = find_entity(name)
        if existing:
            return existing
    new_id = editor.ToolsApplicationRequestBus(bus.Broadcast, "CreateNewEntity", parent if parent else entity.EntityId())
    editor.EditorEntityAPIBus(bus.Event, "SetName", new_id, name)
    if parent:
        components.TransformBus(bus.Event, "SetLocalTranslation", new_id, vec3(position))
    else:
        components.TransformBus(bus.Event, "SetWorldTranslation", new_id, vec3(position))
    return new_id


def delete_tree(entity_id):
    if entity_id:
        editor.ToolsApplicationRequestBus(bus.Broadcast, "DeleteEntitiesAndAllDescendants", [entity_id])


def get_component(entity_id, component_name):
    """Returns the component, adding it first if the entity doesn't have one."""
    type_ids = editor.EditorComponentAPIBus(
        bus.Broadcast, "FindComponentTypeIdsByEntityType", [component_name], entity.EntityType().Game)
    if not type_ids or type_ids[0].IsNull():
        print(f"!! no component type '{component_name}'")
        return None
    type_id = type_ids[0]
    if editor.EditorComponentAPIBus(bus.Broadcast, "HasComponentOfType", entity_id, type_id):
        return editor.EditorComponentAPIBus(bus.Broadcast, "GetComponentOfType", entity_id, type_id).GetValue()
    outcome = editor.EditorComponentAPIBus(bus.Broadcast, "AddComponentsOfType", entity_id, [type_id])
    if not outcome.IsSuccess():
        print(f"!! could not add '{component_name}'")
        return None
    return outcome.GetValue()[0]


def property_paths(component):
    tree = editor.EditorComponentAPIBus(bus.Broadcast, "BuildComponentPropertyTreeEditor", component)
    return tree.GetValue().build_paths_list() if tree.IsSuccess() else []


def set_property(component, path, value, quiet=False):
    outcome = editor.EditorComponentAPIBus(bus.Broadcast, "SetComponentProperty", component, path, value)
    if not quiet or not outcome.IsSuccess():
        print(("ok   " if outcome.IsSuccess() else "FAIL ") + path)
    return outcome.IsSuccess()


def get_property(component, path):
    outcome = editor.EditorComponentAPIBus(bus.Broadcast, "GetComponentProperty", component, path)
    return outcome.GetValue() if outcome.IsSuccess() else None


def set_scale(entity_id, size):
    editor.EditorComponentAPIBus(bus.Broadcast, "AddComponentsOfType", entity_id, [NON_UNIFORM_SCALE])
    entity.NonUniformScaleRequestBus(bus.Event, "SetScale", entity_id, vec3(size))


def make_box(name, parent=None, position=(0.0, 0.0, 0.0), size=(1.0, 1.0, 1.0), material_name="wallpaper", collide=True):
    box = create_entity(name, parent, position, reuse=False)
    set_scale(box, size)
    mesh = get_component(box, "Mesh")
    set_property(mesh, "Controller|Configuration|Model Asset", asset_id(BOX_MODEL_PATH), quiet=True)
    mat = get_component(box, "Material")
    set_property(mat, "Default Material|Material Asset", material(material_name), quiet=True)
    if collide:
        get_component(box, "PhysX Static Rigid Body")
        get_component(box, "PhysX Primitive Collider")
    return box


def make_point_light(name, parent=None, position=(0.0, 0.0, 0.0), intensity=150.0, radius=8.0,
                     color=(1.0, 0.96, 0.85), shadows=False):
    light_entity = create_entity(name, parent, position, reuse=False)
    light = get_component(light_entity, "Light")
    set_property(light, "Controller|Configuration|Light type", 6, quiet=True)  # simple point
    set_property(light, "Controller|Configuration|Color", azmath.Color(color[0], color[1], color[2], 1.0), quiet=True)
    set_property(light, "Controller|Configuration|Intensity", float(intensity), quiet=True)
    set_property(light, "Controller|Configuration|Attenuation radius|Mode", 0, quiet=True)
    set_property(light, "Controller|Configuration|Attenuation radius|Radius", float(radius), quiet=True)
    if shadows:
        set_property(light, "Controller|Configuration|Shadows|Enable shadow", True, quiet=True)
    return light_entity


def look_from(x, y, z, pitch_degrees=0.0, yaw_degrees=0.0):
    """Moves the editor camera (for screenshots)."""
    general.set_current_view_position(float(x), float(y), float(z))
    general.set_current_view_rotation(float(pitch_degrees), 0.0, float(yaw_degrees))
