"""Author the line-art parameter collection and the three master materials.

Creates, or rebuilds in place:

    /LineArtCore/Materials/MPC_LineArtEnvironment   shared environment truth
    /LineArtCore/Materials/M_LineArt_Fill           flat fill, Unlit, per-instance tint
    /LineArtCore/Materials/M_LineArt_Outline        inverted-hull ink outline
    /LineArtCore/Materials/M_LineArt_CharacterInk   legs / eyes / mouth, always pure black
    /LineArtCore/Materials/MI_LineArt_Paper        backdrop tint, for a preview level

Idempotent: every material's graph is deleted and rebuilt from this script, and the
collection's parameter arrays are rebuilt by stable parameter name.  Running it twice must
leave byte-identical assets.

The parameter names must stay in step with two files this script cannot import:

    Plugins/LineArtCore/Source/LineArtCoreRuntime/Public/LineArtParameterNames.h
    Plugins/LineArtCore/Shaders/LineArtEnvironment.ush

A drifting name does not fail loudly on its own -- the material still compiles and simply
renders a stale value -- so the runtime subsystem checks every write and logs the first
miss per name under LogLineArtCore.

Run in the Output Log's **Python input mode**, NOT Cmd mode -- the mode dropdown sits to
the LEFT of the input box. Cmd mode only accepts engine console commands; it discards a
bare ``import`` silently, so the script appears to run and do nothing. The only Python
entry allowed in Cmd mode is ``py "<script>"``.

Run in the full Unreal Editor (NOT in PIE). Output Log, Python input mode -- UE puts
every enabled plugin's Content/Python on sys.path, so no path is needed or wanted:

    import importlib, CreateLineArtCoreAssets
    importlib.reload(CreateLineArtCoreAssets)
    CreateLineArtCoreAssets.main()
"""

import unreal


MATERIAL_ROOT = "/LineArtCore/Materials"
COLLECTION_PATH = f"{MATERIAL_ROOT}/MPC_LineArtEnvironment"
FILL_PATH = f"{MATERIAL_ROOT}/M_LineArt_Fill"
OUTLINE_PATH = f"{MATERIAL_ROOT}/M_LineArt_Outline"
CHARACTER_INK_PATH = f"{MATERIAL_ROOT}/M_LineArt_CharacterInk"
PAPER_INSTANCE_PATH = f"{MATERIAL_ROOT}/MI_LineArt_Paper"

#: The paper the whole style is drawn on: SkyLand's 0xFDFBF6, in linear.
PAPER_COLOR = unreal.LinearColor(0.9823, 0.9647, 0.9216, 1.0)

SHADER_INCLUDE = "/Plugin/LineArtCore/LineArtEnvironment.ush"

MAX_POINT_LIGHTS = 4

# (name, default) pairs owned by this script.  Rebuilt by name on every run.
SCALAR_PARAMETERS = [
    ("Daylight", 1.0),
    ("GridOpacity", 0.34),
    ("OutlineThickness", 1.0),
    ("FogNear", 2200.0),
    ("FogFar", 5200.0),
    ("ScatterStrength", 0.0),
    ("CloudShadowStrength", 0.0),
]

VECTOR_PARAMETERS = [
    # Base ink 0x171614 and grid 0x9d9a90, matching lineMaterials.ts.
    ("InkColor", unreal.LinearColor(0.0080, 0.0073, 0.0061, 1.0)),
    ("GridColor", unreal.LinearColor(0.3372, 0.3231, 0.2786, 1.0)),
    ("AmbientColor", unreal.LinearColor(1.0, 1.0, 1.0, 1.0)),
    ("SunDirection", unreal.LinearColor(-0.55, 0.35, 0.9, 0.0)),
    ("SkyTint", unreal.LinearColor(1.0, 1.0, 1.0, 1.0)),
    ("BounceTint", unreal.LinearColor(1.0, 1.0, 1.0, 1.0)),
    ("ScatterColor", unreal.LinearColor(1.0, 1.0, 1.0, 1.0)),
    ("FogColor", unreal.LinearColor(0.992, 0.984, 0.965, 1.0)),
    ("CloudShadowOffset", unreal.LinearColor(0.0, 0.0, 0.0, 0.0)),
]

for _slot in range(MAX_POINT_LIGHTS):
    # w of Position is the radius; a is this frame's intensity, 0 meaning "empty slot".
    VECTOR_PARAMETERS.append((f"PointLight{_slot}Position", unreal.LinearColor(0.0, 0.0, 0.0, 1.0)))
    VECTOR_PARAMETERS.append((f"PointLight{_slot}Color", unreal.LinearColor(0.0, 0.0, 0.0, 0.0)))
    VECTOR_PARAMETERS.append((f"PointLight{_slot}EdgeColor", unreal.LinearColor(0.0, 0.0, 0.0, 1.0)))


#: (material, requested name, resolved name, id readable/valid) for every CollectionParameter
#: node this run created. Emptied at the start of main(); reported just before the marker.
NODE_DIAGNOSTICS = []


def log(message):
    unreal.log(f"[CreateLineArtCoreAssets] {message}")


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


def require_editor_asset_mode():
    """Block content-asset edits while PIE owns runtime references to those assets."""
    subsystem = require(
        unreal.get_editor_subsystem(unreal.LevelEditorSubsystem),
        "LevelEditorSubsystem is unavailable; run this script in the full Unreal Editor",
    )
    require(
        not subsystem.is_in_play_in_editor(),
        "Cannot author line-art assets while Play/PIE is active. Click Stop, then run this script again.",
    )


def set_property(target, name, value):
    """set_editor_property with the failure turned into an actionable message.

    UE 5.7 does not expose every field of every wrapper, and a bare AttributeError here
    reads as 'the script is broken' rather than 'this property moved'.
    """
    try:
        target.set_editor_property(name, value)
    except Exception as error:
        raise RuntimeError(
            f"Cannot set '{name}' on {type(target).__name__}: {error}. "
            f"Check the UE 5.7 Python exposure for this property before editing this script."
        ) from error


def make_struct(struct_type, **fields):
    """Build a USTRUCT wrapper, preferring keyword construction.

    Two documented failure modes live here (see the Python failure ledger): some wrappers
    reject argument construction outright, and some fields are EditDefaultsOnly and cannot
    be written on an instance. Try the keyword form first, fall back to field assignment,
    and report precisely which one failed rather than guessing.
    """
    try:
        return struct_type(**fields)
    except Exception:
        instance = struct_type()
        for key, value in fields.items():
            set_property(instance, key, value)
        return instance


def guid_field_is_valid(owner, name):
    """True / False / None for a GUID field, where None means "Python cannot see it".

    FCollectionParameterBase::Id is protected: reading it can raise just as writing it does.
    The three-way answer matters -- reporting a wrapper limitation as an invalid GUID is
    what sent the last round of debugging after a root cause that did not exist.
    """
    try:
        guid = owner.get_editor_property(name)
    except Exception:
        return None
    if guid is None:
        return False
    try:
        return any(int(guid.get_editor_property(field)) != 0 for field in ("a", "b", "c", "d"))
    except Exception:
        return None


def package_path(asset):
    return "" if asset is None else str(asset.get_path_name()).split(".", 1)[0]


def asset_tools():
    return require(unreal.AssetToolsHelpers.get_asset_tools(), "AssetTools is unavailable")


def create_or_load(asset_path, factory, asset_class):
    """Load the asset if it exists, otherwise create it. Never silently replaces one."""
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        asset = require(
            unreal.EditorAssetLibrary.load_asset(asset_path),
            f"{asset_path} exists but could not be loaded; delete it by hand and re-run.",
        )
        require(
            isinstance(asset, asset_class),
            f"{asset_path} exists but is a {type(asset).__name__}, not {asset_class.__name__}.",
        )
        return asset

    package_name = asset_path.rsplit("/", 1)[0]
    asset_name = asset_path.rsplit("/", 1)[1]
    return require(
        asset_tools().create_asset(asset_name, package_name, asset_class, factory),
        f"Unable to create {asset_path}",
    )


def build_collection():
    """Rebuild the parameter collection's arrays by stable parameter name.

    Filtering is by NAME STRING, never by wrapper identity: comparing USTRUCT wrappers with
    `in` / `not in` compares object identity, the filter never matches, and the rebuild
    quietly degrades into an append-only accumulator that doubles the array on every run.
    """
    collection = create_or_load(
        COLLECTION_PATH,
        unreal.MaterialParameterCollectionFactoryNew(),
        unreal.MaterialParameterCollection,
    )

    owned_scalars = {name for name, _ in SCALAR_PARAMETERS}
    owned_vectors = {name for name, _ in VECTOR_PARAMETERS}

    preserved_scalars = [
        entry for entry in collection.get_editor_property("scalar_parameters")
        if str(entry.get_editor_property("parameter_name")) not in owned_scalars
    ]
    preserved_vectors = [
        entry for entry in collection.get_editor_property("vector_parameters")
        if str(entry.get_editor_property("parameter_name")) not in owned_vectors
    ]

    # No Id is written here: FCollectionParameterBase::Id is protected and the struct's own
    # constructor assigns one. An earlier attempt to set it by hand was a guess and failed.
    scalars = list(preserved_scalars) + [
        make_struct(
            unreal.CollectionScalarParameter,
            parameter_name=unreal.Name(name),
            default_value=value,
        )
        for name, value in SCALAR_PARAMETERS
    ]
    vectors = list(preserved_vectors) + [
        make_struct(
            unreal.CollectionVectorParameter,
            parameter_name=unreal.Name(name),
            default_value=value,
        )
        for name, value in VECTOR_PARAMETERS
    ]

    set_property(collection, "scalar_parameters", scalars)
    set_property(collection, "vector_parameters", vectors)

    # Read back by length, not by membership: an existence check returns True for duplicates
    # and so cannot detect the accumulator failure this rebuild exists to prevent.
    final_scalars = list(collection.get_editor_property("scalar_parameters"))
    final_vectors = list(collection.get_editor_property("vector_parameters"))
    require(
        len(final_scalars) == len(preserved_scalars) + len(SCALAR_PARAMETERS),
        f"Collection kept {len(final_scalars)} scalars, expected "
        f"{len(preserved_scalars) + len(SCALAR_PARAMETERS)}",
    )
    require(
        len(final_vectors) == len(preserved_vectors) + len(VECTOR_PARAMETERS),
        f"Collection kept {len(final_vectors)} vectors, expected "
        f"{len(preserved_vectors) + len(VECTOR_PARAMETERS)}",
    )

    # Verify the NAMES round-tripped. The previous version only checked array lengths, which
    # is why a collection full of parameters the materials could not resolve still passed.
    stored_names = {str(entry.get_editor_property("parameter_name"))
                    for entry in final_scalars + final_vectors}
    missing = sorted((owned_scalars | owned_vectors) - stored_names)
    require(not missing, f"Collection did not retain these parameter names: {missing}")

    # Id validity is only reported: the field is protected, so an unreadable id is a
    # wrapper limitation, not evidence of a defect. See PY-UE-010.
    unreadable = sum(
        1 for entry in final_scalars + final_vectors
        if guid_field_is_valid(entry, "id") is None
    )
    if unreadable:
        log(f"Note: {unreadable} parameter id(s) are not readable from Python (protected field)")

    log(f"Collection ready: {len(final_scalars)} scalars, {len(final_vectors)} vectors")
    return collection


class GraphBuilder:
    """Thin wrapper over MaterialEditingLibrary that caches one node per collection parameter."""

    def __init__(self, material, collection):
        self.material = material
        self.collection = collection
        self.collection_nodes = {}
        self.node_x = -1600
        self.node_y = -600

    def expression(self, expression_class, offset_x=0, offset_y=0):
        node = require(
            unreal.MaterialEditingLibrary.create_material_expression(
                self.material, expression_class, self.node_x + offset_x, self.node_y + offset_y
            ),
            f"Unable to create {expression_class.__name__} in {package_path(self.material)}",
        )
        self.node_y += 120
        return node

    def collection_parameter(self, parameter_name):
        if parameter_name in self.collection_nodes:
            return self.collection_nodes[parameter_name]

        node = self.expression(unreal.MaterialExpressionCollectionParameter)
        set_property(node, "collection", self.collection)
        set_property(node, "parameter_name", unreal.Name(parameter_name))

        # Record, do not abort. Two materials failed to compile with "invalid parameter None"
        # while a per-node assertion passed, so the useful output here is the FULL picture of
        # which nodes resolved and which did not -- aborting on the first one hides the rest
        # and the next run learns nothing new. main() refuses the success marker if any
        # entry below is bad.
        resolved = str(node.get_editor_property("parameter_name"))
        node_id_valid = guid_field_is_valid(node, "parameter_id")
        NODE_DIAGNOSTICS.append((package_path(self.material), parameter_name, resolved, node_id_valid))
        self.collection_nodes[parameter_name] = node
        return node

    def custom(self, description, code, inputs, output_type=None):
        """A Custom HLSL node including the ported line-art shader.

        inputs is a list of (input_name, source_expression, source_output_name).
        """
        node = self.expression(unreal.MaterialExpressionCustom)
        set_property(node, "description", description)
        set_property(node, "code", code)
        set_property(
            node,
            "output_type",
            output_type if output_type is not None else unreal.CustomMaterialOutputType.CMOT_FLOAT3,
        )
        set_property(
            node,
            "inputs",
            [make_struct(unreal.CustomInput, input_name=unreal.Name(name)) for name, _, _ in inputs],
        )
        # The include is what makes this node more than a wall of inline HLSL. If the
        # property ever moves, fail here rather than shipping a material that silently
        # compiles to the default output.
        set_property(node, "include_file_paths", [SHADER_INCLUDE])

        for input_name, source, source_output in inputs:
            require(
                unreal.MaterialEditingLibrary.connect_material_expressions(
                    source, source_output, node, input_name
                ),
                f"Unable to connect {input_name} on Custom node '{description}'",
            )
        return node

    def connect(self, source, source_output, target, target_input):
        require(
            unreal.MaterialEditingLibrary.connect_material_expressions(
                source, source_output, target, target_input
            ),
            f"Unable to connect {source_output} -> {target_input}",
        )

    def connect_property(self, source, source_output, material_property):
        require(
            unreal.MaterialEditingLibrary.connect_material_property(source, source_output, material_property),
            f"Unable to connect {source_output} to {material_property}",
        )


def set_property_optional(target, name, value, reason):
    """For flags that are an optimisation, not correctness. Warns instead of aborting."""
    try:
        target.set_editor_property(name, value)
    except Exception as error:
        unreal.log_warning(
            f"[CreateLineArtCoreAssets] Could not set optional '{name}' ({reason}): {error}"
        )


FILL_CODE = """
return LineArtSurfaceColor(
    BaseColor.rgb, WorldNormal.xyz, WorldPosition.xyz, SunDirection.xyz, Daylight,
    AmbientColor.rgb, SkyTint.rgb, BounceTint.rgb, CloudShadowOffset.xy, CloudShadowStrength);
"""

POINT_LIGHT_CODE = """
float3 Radiance = float3(0.0f, 0.0f, 0.0f);
Radiance += LineArtPointLightRadiance(WorldPosition.xyz, WorldNormal.xyz, Light0Position, Light0Color, Light0Edge.rgb);
Radiance += LineArtPointLightRadiance(WorldPosition.xyz, WorldNormal.xyz, Light1Position, Light1Color, Light1Edge.rgb);
Radiance += LineArtPointLightRadiance(WorldPosition.xyz, WorldNormal.xyz, Light2Position, Light2Color, Light2Edge.rgb);
Radiance += LineArtPointLightRadiance(WorldPosition.xyz, WorldNormal.xyz, Light3Position, Light3Color, Light3Edge.rgb);
return Radiance;
"""

FOG_CODE = """
return LineArtApplyFog(
    Color.rgb, WorldPosition.xyz, CameraPosition.xyz, SunDirection.xyz,
    FogColor.rgb, ScatterColor.rgb, ScatterStrength, FogNear, FogFar);
"""

# View.ViewToClip and View.ViewSizeAndInvSize are engine-side names; if a future engine
# version renames them the material fails to compile, which is the loud failure we want
# here -- an outline silently reverting to constant world thickness is the bad outcome.
OUTLINE_THICKNESS_CODE = """
float TanHalfFov = 1.0f / max(View.ViewToClip[1][1], 0.0001f);
return LineArtOutlineThickness(PixelDepth, ThicknessPixels, View.ViewSizeAndInvSize.y, TanHalfFov);
"""

# Cull the front faces of the hull. UE has no front-face cull switch, so the two-sided
# material discards the side facing the camera and keeps the extruded back side.
OUTLINE_MASK_CODE = """
return saturate(-Parameters.TwoSidedSign);
"""


def build_fill_material(collection):
    material = create_or_load(FILL_PATH, unreal.MaterialFactoryNew(), unreal.Material)
    unreal.MaterialEditingLibrary.delete_all_material_expressions(material)

    set_property(material, "shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    set_property(material, "blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    # The reference project draws fills double sided; low-poly shells have no back faces to
    # hide behind and single-sided leaves visible holes when the camera clips a slope.
    set_property(material, "two_sided", True)
    set_property_optional(material, "used_with_instanced_static_meshes", True,
                          "HISM batches will prompt for usage on first apply")

    builder = GraphBuilder(material, collection)

    base_tint = builder.expression(unreal.MaterialExpressionVectorParameter)
    set_property(base_tint, "parameter_name", unreal.Name("BaseColor"))
    set_property(base_tint, "default_value", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))

    vertex_color = builder.expression(unreal.MaterialExpressionVertexColor)

    # Parameter times vertex colour: one master material serves both a uniformly coloured
    # mesh and a batch whose colours can only travel per vertex / per instance.
    base_color = builder.expression(unreal.MaterialExpressionMultiply)
    builder.connect(base_tint, "", base_color, "A")
    builder.connect(vertex_color, "", base_color, "B")

    world_normal = builder.expression(unreal.MaterialExpressionVertexNormalWS)
    world_position = builder.expression(unreal.MaterialExpressionWorldPosition)
    camera_position = builder.expression(unreal.MaterialExpressionCameraPositionWS)

    surface = builder.custom("LineArtFill", FILL_CODE, [
        ("BaseColor", base_color, ""),
        ("WorldNormal", world_normal, ""),
        ("WorldPosition", world_position, ""),
        ("SunDirection", builder.collection_parameter("SunDirection"), ""),
        ("Daylight", builder.collection_parameter("Daylight"), ""),
        ("AmbientColor", builder.collection_parameter("AmbientColor"), ""),
        ("SkyTint", builder.collection_parameter("SkyTint"), ""),
        ("BounceTint", builder.collection_parameter("BounceTint"), ""),
        ("CloudShadowOffset", builder.collection_parameter("CloudShadowOffset"), ""),
        ("CloudShadowStrength", builder.collection_parameter("CloudShadowStrength"), ""),
    ])

    point_lights = builder.custom("LineArtPointLights", POINT_LIGHT_CODE, [
        ("WorldPosition", world_position, ""),
        ("WorldNormal", world_normal, ""),
    ] + [
        (f"Light{slot}{suffix}", builder.collection_parameter(f"PointLight{slot}{parameter}"), "")
        for slot in range(MAX_POINT_LIGHTS)
        for suffix, parameter in (("Position", "Position"), ("Color", "Color"), ("Edge", "EdgeColor"))
    ])

    # Point lights are added before fog on purpose: firelight has to be eaten by distance
    # like everything else, or a campfire stays a glowing dot half a map away.
    lit = builder.expression(unreal.MaterialExpressionAdd)
    builder.connect(surface, "", lit, "A")
    builder.connect(point_lights, "", lit, "B")

    fogged = builder.custom("LineArtFog", FOG_CODE, [
        ("Color", lit, ""),
        ("WorldPosition", world_position, ""),
        ("CameraPosition", camera_position, ""),
        ("SunDirection", builder.collection_parameter("SunDirection"), ""),
        ("FogColor", builder.collection_parameter("FogColor"), ""),
        ("ScatterColor", builder.collection_parameter("ScatterColor"), ""),
        ("ScatterStrength", builder.collection_parameter("ScatterStrength"), ""),
        ("FogNear", builder.collection_parameter("FogNear"), ""),
        ("FogFar", builder.collection_parameter("FogFar"), ""),
    ])

    builder.connect_property(fogged, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    unreal.MaterialEditingLibrary.recompile_material(material)
    return material


def build_outline_material(collection):
    material = create_or_load(OUTLINE_PATH, unreal.MaterialFactoryNew(), unreal.Material)
    unreal.MaterialEditingLibrary.delete_all_material_expressions(material)

    set_property(material, "shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    set_property(material, "blend_mode", unreal.BlendMode.BLEND_MASKED)
    set_property(material, "two_sided", True)
    set_property(material, "opacity_mask_clip_value", 0.33)
    set_property_optional(material, "used_with_instanced_static_meshes", True,
                          "HISM batches will prompt for usage on first apply")

    builder = GraphBuilder(material, collection)

    builder.connect_property(
        builder.collection_parameter("InkColor"), "", unreal.MaterialProperty.MP_EMISSIVE_COLOR
    )

    mask = builder.custom("OutlineFrontFaceMask", OUTLINE_MASK_CODE, [],
                          unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    builder.connect_property(mask, "", unreal.MaterialProperty.MP_OPACITY_MASK)

    thickness = builder.custom("OutlineThickness", OUTLINE_THICKNESS_CODE, [
        ("PixelDepth", builder.expression(unreal.MaterialExpressionPixelDepth), ""),
        ("ThicknessPixels", builder.collection_parameter("OutlineThickness"), ""),
    ], unreal.CustomMaterialOutputType.CMOT_FLOAT1)

    # Extruding along the VERTEX normal, not the pixel normal: hard-edged low-poly meshes
    # need averaged normals baked into the asset, otherwise the hull splits open at every
    # crease. See the migration doc -- this is the step that most often goes wrong.
    hull_normal = builder.expression(unreal.MaterialExpressionVertexNormalWS)
    offset = builder.expression(unreal.MaterialExpressionMultiply)
    builder.connect(hull_normal, "", offset, "A")
    builder.connect(thickness, "", offset, "B")
    builder.connect_property(offset, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)

    unreal.MaterialEditingLibrary.recompile_material(material)
    return material


def build_character_ink_material():
    """Legs, eyes and mouth: one flat black that never answers to the sky.

    These marks are not a colour on the body, they are the cue for who this is, which way it
    faces and whether it is walking. Cues read best in exactly one colour, so this layer sits
    out the environment ink swap, the fog and the tonemapper alike -- distance fog applies to
    unlit materials too, and on a foggy night it turns black legs into pale smears on grey
    paper.
    """
    material = create_or_load(CHARACTER_INK_PATH, unreal.MaterialFactoryNew(), unreal.Material)
    unreal.MaterialEditingLibrary.delete_all_material_expressions(material)

    set_property(material, "shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    set_property(material, "blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    set_property(material, "two_sided", True)

    builder = GraphBuilder(material, None)
    ink = builder.expression(unreal.MaterialExpressionVectorParameter)
    set_property(ink, "parameter_name", unreal.Name("InkColor"))
    set_property(ink, "default_value", unreal.LinearColor(0.0, 0.0, 0.0, 1.0))

    builder.connect_property(ink, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    unreal.MaterialEditingLibrary.recompile_material(material)
    return material


def build_paper_instance(character_ink):
    """The backdrop.

    Without one, the scene's background is the engine's default grey-blue and the ink lines
    read as stickers floating on it rather than marks on paper. M_LineArt_CharacterInk is
    already exactly the right material for the job -- Unlit, two-sided, one flat colour
    parameter -- and two-sided is what lets a camera inside the backdrop sphere see it.
    """
    instance = create_or_load(
        PAPER_INSTANCE_PATH,
        unreal.MaterialInstanceConstantFactoryNew(),
        unreal.MaterialInstanceConstant,
    )
    set_property(instance, "parent", character_ink)
    unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(
        instance, unreal.Name("InkColor"), PAPER_COLOR
    )
    unreal.MaterialEditingLibrary.update_material_instance(instance)
    return instance


def configure_project_settings(collection, fill, outline):
    """Point Line Art Core at what was just authored, so nothing has to be wired by hand.

    Best effort: a failure here costs three clicks in Project Settings, not the assets, so
    it warns rather than aborting. Without it the runtime logs "No FillMaterial configured"
    once per preview actor and draws untextured white.
    """
    settings = getattr(unreal, "LineArtCoreSettings", None)
    if settings is None:
        # Absent from `unreal` does NOT mean the module is off. The C++ subsystem logs under
        # LogLineArtCore whether or not Python can see the class: an editor that was running
        # when the module was built keeps the old reflection bindings. Say both, or the
        # reader goes hunting through plugin settings for a problem that is not there.
        unreal.log_warning(
            "[CreateLineArtCoreAssets] unreal.LineArtCoreSettings is not exposed to Python. "
            "Most likely the editor still holds pre-build bindings -- RESTART THE EDITOR and "
            "re-run. (Check LogLineArtCore: if it logs at all, the module is loaded and this "
            "is only a stale-binding problem.) Until then set Project Settings > Game > "
            "Line Art Core by hand."
        )
        return False

    defaults = unreal.get_default_object(settings)
    try:
        defaults.set_editor_property("environment_collection", collection)
        defaults.set_editor_property("fill_material", fill)
        defaults.set_editor_property("outline_material", outline)
    except Exception as error:
        unreal.log_warning(f"[CreateLineArtCoreAssets] Could not write Line Art Core settings: {error}")
        return False

    save_config = getattr(defaults, "save_config", None)
    if save_config is None:
        unreal.log_warning(
            "[CreateLineArtCoreAssets] Settings written in memory but save_config is not exposed; "
            "open Project Settings > Game > Line Art Core and press Set as Default to persist."
        )
        return False

    save_config()
    log("Project Settings > Game > Line Art Core now points at the authored assets")
    return True


def report_node_diagnostics():
    """Print what every CollectionParameter node actually resolved to, then refuse the
    success marker if any of them is wrong.

    This exists because the last two runs produced assets that saved, validated and rendered
    thumbnails while two materials silently fell back to the Default Material. A marker that
    can print over broken materials is worse than no marker.
    """
    broken = [row for row in NODE_DIAGNOSTICS if row[2] != row[1]]

    log(f"CollectionParameter nodes created: {len(NODE_DIAGNOSTICS)}, mis-resolved: {len(broken)}")
    unreadable_ids = sum(1 for row in NODE_DIAGNOSTICS if row[3] is None)
    if unreadable_ids:
        log(f"  ({unreadable_ids} node id(s) not readable from Python; that alone is not a defect)")
    zero_ids = [row for row in NODE_DIAGNOSTICS if row[3] is False]
    if zero_ids:
        log(f"  {len(zero_ids)} node(s) carry an invalid id even though the name resolved:")
        for material, requested, _resolved, _valid in zero_ids[:10]:
            log(f"    {material} :: {requested}")

    for material, requested, resolved, _valid in broken:
        unreal.log_error(
            f"[CreateLineArtCoreAssets] {material} :: asked for '{requested}', node reports "
            f"'{resolved}'"
        )

    require(
        not broken,
        f"{len(broken)} CollectionParameter node(s) did not resolve; the materials would fall "
        f"back to the Default Material. The lines above name every one of them.",
    )


def main():
    require_editor_asset_mode()
    NODE_DIAGNOSTICS.clear()

    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous([MATERIAL_ROOT], True)

    collection = build_collection()
    fill = build_fill_material(collection)
    outline = build_outline_material(collection)
    character_ink = build_character_ink_material()
    paper = build_paper_instance(character_ink)

    configure_project_settings(collection, fill, outline)

    for asset in (collection, fill, outline, character_ink, paper):
        path = package_path(asset)
        require(unreal.EditorAssetLibrary.save_asset(path, False), f"Unable to save {path}")
        log(f"Saved {path}")

    # Saving is not compiling. Materials save, render thumbnails and validate clean while
    # still failing to compile and falling back to the Default Material at runtime, so the
    # marker is withheld until every collection node is known to resolve.
    report_node_diagnostics()
    log("Now also check the log for 'Failed to compile Material' -- saving proves nothing about it")
    log("LINEART_CORE_ASSETS_OK")


if __name__ == "__main__":
    main()
