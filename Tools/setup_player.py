"""
Noclip Protocol - one-shot level setup for the O3DE Editor.

Builds, in the currently open level:
  Player (PhysX Character Controller + First Person Controller + Flashlight)
    PlayerCamera (Camera, eye height)
      FlashlightBeam (Light, spot/disk, pointing forward)
  GroundCollider (PhysX Static Rigid Body + box collider under the ground plane)

Run it from the Editor console:
  pyRunFile /var/home/chris/O3DE/Projects/NoclipProtocol/Tools/setup_player.py

Safe to run again: anything it already made is skipped.
Everything it does is printed to the console and Editor.log with a [Noclip] tag.
"""

import math as pymath

import azlmbr.bus as bus
import azlmbr.components as components
import azlmbr.editor as editor
import azlmbr.entity as entity
import azlmbr.legacy.general as general
import azlmbr.math as azmath

TAG = "[Noclip]"


def log(msg):
    print(f"{TAG} {msg}")


def find_entity(name):
    search_filter = entity.SearchFilter()
    search_filter.names = [name]
    found = entity.SearchBus(bus.Broadcast, "SearchEntities", search_filter)
    for entity_id in found or []:
        if entity_id.IsValid():
            return entity_id
    return None


def create_entity(name, parent=None):
    existing = find_entity(name)
    if existing:
        log(f"'{name}' already exists, reusing it")
        return existing, False
    parent_id = parent if parent else entity.EntityId()
    new_id = editor.ToolsApplicationRequestBus(bus.Broadcast, "CreateNewEntity", parent_id)
    editor.EditorEntityAPIBus(bus.Event, "SetName", new_id, name)
    log(f"created '{name}'")
    return new_id, True


def set_position(entity_id, x, y, z):
    components.TransformBus(bus.Event, "SetLocalTranslation", entity_id, azmath.Vector3(x, y, z))


def set_rotation_degrees(entity_id, x, y, z):
    components.TransformBus(
        bus.Event, "SetLocalRotation", entity_id,
        azmath.Vector3(pymath.radians(x), pymath.radians(y), pymath.radians(z)))


def get_component(entity_id, component_name):
    """Returns the component on the entity, adding it if it isn't there yet."""
    type_ids = editor.EditorComponentAPIBus(
        bus.Broadcast, "FindComponentTypeIdsByEntityType", [component_name], entity.EntityType().Game)
    if not type_ids or not type_ids[0] or type_ids[0].IsNull():
        log(f"!! component '{component_name}' not found in this build")
        return None
    type_id = type_ids[0]

    has = editor.EditorComponentAPIBus(bus.Broadcast, "HasComponentOfType", entity_id, type_id)
    if has:
        outcome = editor.EditorComponentAPIBus(bus.Broadcast, "GetComponentOfType", entity_id, type_id)
        if outcome.IsSuccess():
            return outcome.GetValue()

    outcome = editor.EditorComponentAPIBus(bus.Broadcast, "AddComponentsOfType", entity_id, [type_id])
    if not outcome.IsSuccess():
        log(f"!! could not add '{component_name}'")
        return None
    log(f"  + {component_name}")
    return outcome.GetValue()[0]


def property_paths(component):
    try:
        tree = editor.EditorComponentAPIBus(bus.Broadcast, "BuildComponentPropertyTreeEditor", component)
        if tree.IsSuccess():
            return tree.GetValue().build_paths_list()
    except Exception as error:  # noqa: BLE001
        log(f"!! could not list properties: {error}")
    return []


def set_property(component, label, candidate_paths, value):
    """Tries each path; on failure prints the component's real property paths so they can be fixed."""
    if component is None:
        return False
    for path in candidate_paths:
        outcome = editor.EditorComponentAPIBus(bus.Broadcast, "SetComponentProperty", component, path, value)
        if outcome.IsSuccess():
            log(f"  set {label}")
            return True
    log(f"!! could not set {label}; this component's properties are:")
    for path in property_paths(component):
        log(f"     {path}")
    return False


def main():
    level = general.get_current_level_name()
    if not level:
        log("no level open - opening DefaultLevel first")
        general.open_level_no_prompt("DefaultLevel")
        general.idle_wait_frames(30)
        level = general.get_current_level_name()
    if not level:
        log("!! could not open a level. Open one with File > Open Level, then run this again.")
        return
    general.idle_wait_frames(1)
    log(f"setting up the player in level '{level}'...")

    # --- An older camera in the level would fight ours for "active camera" ---
    for old_camera_name in ("Camera",):
        old_camera = find_entity(old_camera_name)
        if old_camera:
            cam = get_component(old_camera, "Camera")
            set_property(cam, f"'{old_camera_name}' not auto-active",
                         ["Controller|Configuration|Make active camera on activation?",
                          "Controller|Configuration|Make active camera on activation"], False)

    # --- Player ---
    player, made = create_entity("Player")
    if made:
        set_position(player, 0.0, 0.0, 1.0)
    controller = get_component(player, "PhysX Character Controller")
    set_property(controller, "capsule height 1.8",
                 ["Shape Configuration|Capsule|Height", "Configuration|Shape Configuration|Capsule|Height"], 1.8)
    set_property(controller, "capsule radius 0.3",
                 ["Shape Configuration|Capsule|Radius", "Configuration|Shape Configuration|Capsule|Radius"], 0.3)
    fps = get_component(player, "First Person Controller")
    flashlight = get_component(player, "Flashlight")

    # --- Camera at eye height ---
    camera, made = create_entity("PlayerCamera", player)
    if made:
        set_position(camera, 0.0, 0.0, 0.7)
    camera_component = get_component(camera, "Camera")
    set_property(camera_component, "camera auto-active",
                 ["Controller|Configuration|Make active camera on activation?",
                  "Controller|Configuration|Make active camera on activation"], True)

    # --- Flashlight beam: disk lights shine along +Z, the camera looks along +Y,
    #     so tip it -90 degrees on X to point where the player looks ---
    beam, made = create_entity("FlashlightBeam", camera)
    if made:
        set_position(beam, 0.15, 0.2, -0.15)  # a little right of and below the eyes, like a held torch
        set_rotation_degrees(beam, -90.0, 0.0, 0.0)
    light = get_component(beam, "Light")
    set_property(light, "spot (disk) light", ["Controller|Configuration|Light type"], 2)
    set_property(light, "warm color", ["Controller|Configuration|Color"], azmath.Color(1.0, 0.92, 0.78, 1.0))
    set_property(light, "brightness", ["Controller|Configuration|Intensity"], 400.0)
    set_property(light, "shutters on", ["Controller|Configuration|Shutters|Enable shutters"], True)
    set_property(light, "beam inner angle", ["Controller|Configuration|Shutters|Inner angle"], 12.0)
    set_property(light, "beam outer angle", ["Controller|Configuration|Shutters|Outer angle"], 28.0)
    set_property(light, "shadows on", ["Controller|Configuration|Shadows|Enable shadow"], True)

    # --- Hook the player components up to their child entities ---
    set_property(fps, "Camera entity -> PlayerCamera",
                 ["Camera entity", "First Person Controller|Camera entity", "CameraEntity"], camera)
    set_property(flashlight, "Light entity -> FlashlightBeam",
                 ["Light entity", "Flashlight|Light entity", "LightEntity"], beam)

    # --- Floor collision: a thin wide box just under the ground plane ---
    ground, made = create_entity("GroundCollider")
    if made:
        set_position(ground, 0.0, 0.0, -0.5)
    get_component(ground, "PhysX Static Rigid Body")
    collider = get_component(ground, "PhysX Primitive Collider")
    set_property(collider, "floor size 512 x 512 x 1",
                 ["Shape Configuration|Box|Dimensions"], azmath.Vector3(512.0, 512.0, 1.0))

    general.save_level()
    log("done - level saved. Press Ctrl+G to play.")


main()
