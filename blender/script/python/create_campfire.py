"""Create a stylized campfire in Blender.

Run this file from Blender's Scripting workspace.  Re-running it replaces only
the collection created by this script, leaving the rest of the scene intact.
"""

import math

import bpy
from mathutils import Vector


COLLECTION_NAME = "Campfire_Generated"


def remove_generated_collection():
    collection = bpy.data.collections.get(COLLECTION_NAME)
    if collection is None:
        return

    for obj in list(collection.objects):
        bpy.data.objects.remove(obj, do_unlink=True)
    bpy.data.collections.remove(collection)


def create_collection():
    collection = bpy.data.collections.new(COLLECTION_NAME)
    bpy.context.scene.collection.children.link(collection)
    return collection


def move_to_collection(obj, collection):
    for source in list(obj.users_collection):
        source.objects.unlink(obj)
    collection.objects.link(obj)


def principled_input(node, *names):
    for name in names:
        socket = node.inputs.get(name)
        if socket is not None:
            return socket
    return None


def make_material(name, color, roughness=0.6, metallic=0.0):
    material = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    material.use_nodes = True
    bsdf = material.node_tree.nodes.get("Principled BSDF")
    if bsdf is not None:
        principled_input(bsdf, "Base Color").default_value = (*color, 1.0)
        principled_input(bsdf, "Roughness").default_value = roughness
        principled_input(bsdf, "Metallic").default_value = metallic
    return material


def make_emission_material(name, color, strength):
    material = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    material.use_nodes = True
    nodes = material.node_tree.nodes
    links = material.node_tree.links
    nodes.clear()

    output = nodes.new("ShaderNodeOutputMaterial")
    emission = nodes.new("ShaderNodeEmission")
    emission.inputs["Color"].default_value = (*color, 1.0)
    emission.inputs["Strength"].default_value = strength
    links.new(emission.outputs["Emission"], output.inputs["Surface"])
    return material


def smooth_object(obj):
    if obj.type == "MESH":
        for polygon in obj.data.polygons:
            polygon.use_smooth = True


def add_beveled_cylinder(collection, name, radius, depth, location, rotation, material):
    bpy.ops.mesh.primitive_cylinder_add(
        vertices=12,
        radius=radius,
        depth=depth,
        location=location,
        rotation=rotation,
    )
    obj = bpy.context.object
    obj.name = name
    obj.data.materials.append(material)
    bevel = obj.modifiers.new("Soft_Edges", "BEVEL")
    bevel.width = 0.045
    bevel.segments = 2
    smooth_object(obj)
    move_to_collection(obj, collection)
    return obj


def add_stone_ring(collection, material):
    count = 14
    ring_radius = 1.45
    for index in range(count):
        angle = math.tau * index / count
        location = (ring_radius * math.cos(angle), ring_radius * math.sin(angle), 0.27)
        bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=2, radius=0.48, location=location)
        stone = bpy.context.object
        stone.name = f"Campfire_Stone_{index + 1:02d}"
        stone.scale = (1.12, 0.78, 0.72 + 0.08 * math.sin(index * 2.3))
        stone.rotation_euler = (0.12 * math.sin(index), 0.08 * math.cos(index), angle)
        stone.data.materials.append(material)
        smooth_object(stone)
        move_to_collection(stone, collection)


def add_logs(collection, bark_material, cut_material):
    log_specs = (
        ((0.0, 0.0, 0.63), (0.0, math.radians(68), math.radians(45))),
        ((0.0, 0.0, 0.63), (0.0, math.radians(68), math.radians(135))),
        ((0.0, 0.0, 0.92), (0.0, math.radians(74), math.radians(12))),
    )
    for index, (location, rotation) in enumerate(log_specs, start=1):
        log = add_beveled_cylinder(
            collection,
            f"Campfire_Log_{index:02d}",
            0.24,
            2.25,
            location,
            rotation,
            bark_material,
        )
        # Material slot 1 colors both cylinder end caps like freshly cut wood.
        log.data.materials.append(cut_material)
        for polygon in log.data.polygons:
            if abs(polygon.normal.z) > 0.8:
                polygon.material_index = 1


def add_coals(collection, coal_material, ember_material):
    for index in range(11):
        angle = index * 2.39996
        radius = 0.18 + 0.055 * index
        location = (radius * math.cos(angle), radius * math.sin(angle), 0.34)
        bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=1, radius=0.19, location=location)
        coal = bpy.context.object
        coal.name = f"Campfire_Coal_{index + 1:02d}"
        coal.scale = (1.25, 0.8, 0.55)
        coal.rotation_euler[2] = angle
        coal.data.materials.append(ember_material if index % 3 == 0 else coal_material)
        move_to_collection(coal, collection)


def add_flame_blob(collection, name, material, location, scale, rotation_z=0.0):
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=3, radius=1.0, location=location)
    flame = bpy.context.object
    flame.name = name
    flame.scale = scale
    flame.rotation_euler[2] = rotation_z
    flame.data.materials.append(material)
    smooth_object(flame)
    move_to_collection(flame, collection)
    return flame


def add_flames(collection, red_material, orange_material, yellow_material):
    # Overlapping tapered blobs produce a readable flame silhouette from all angles.
    add_flame_blob(collection, "Flame_Outer_Base", red_material, (0, 0, 0.92), (0.78, 0.65, 0.75))
    add_flame_blob(collection, "Flame_Outer_Tip", red_material, (-0.12, 0.03, 1.65), (0.48, 0.40, 0.92), 0.25)
    add_flame_blob(collection, "Flame_Side_Tongue", orange_material, (0.38, -0.06, 1.42), (0.28, 0.23, 0.70), -0.35)
    add_flame_blob(collection, "Flame_Middle", orange_material, (0.02, -0.03, 1.18), (0.55, 0.46, 0.72))
    add_flame_blob(collection, "Flame_Inner", yellow_material, (0.0, -0.18, 1.03), (0.31, 0.26, 0.48))


def add_light(collection):
    light_data = bpy.data.lights.new("Campfire_Warm_Light", type="POINT")
    light_data.color = (1.0, 0.24, 0.045)
    light_data.energy = 950.0
    light_data.shadow_soft_size = 2.1
    light = bpy.data.objects.new("Campfire_Warm_Light", light_data)
    light.location = Vector((0.0, 0.0, 1.55))
    collection.objects.link(light)


def build_campfire():
    remove_generated_collection()
    collection = create_collection()

    stone = make_material("M_Campfire_Stone", (0.22, 0.20, 0.17), roughness=0.92)
    bark = make_material("M_Campfire_Bark", (0.13, 0.045, 0.018), roughness=0.9)
    cut_wood = make_material("M_Campfire_CutWood", (0.36, 0.16, 0.055), roughness=0.82)
    coal = make_material("M_Campfire_Coal", (0.018, 0.012, 0.009), roughness=0.95)
    ember = make_emission_material("M_Campfire_Ember", (1.0, 0.055, 0.005), 3.0)
    flame_red = make_emission_material("M_Campfire_FlameRed", (1.0, 0.025, 0.002), 3.5)
    flame_orange = make_emission_material("M_Campfire_FlameOrange", (1.0, 0.18, 0.004), 5.0)
    flame_yellow = make_emission_material("M_Campfire_FlameYellow", (1.0, 0.72, 0.08), 7.0)

    add_stone_ring(collection, stone)
    add_logs(collection, bark, cut_wood)
    add_coals(collection, coal, ember)
    add_flames(collection, flame_red, flame_orange, flame_yellow)
    add_light(collection)

    bpy.context.view_layer.objects.active = collection.objects.get("Flame_Inner")
    print("Campfire created in collection: Campfire_Generated")


if __name__ == "__main__":
    build_campfire()
