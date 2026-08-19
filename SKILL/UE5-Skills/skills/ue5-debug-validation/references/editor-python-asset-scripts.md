# Unreal Editor Python Asset-Script Failures

Use this reference when an Unreal Editor Python script creates or configures Blueprint/DataAsset assets and fails during import, class resolution, CDO access, property writes, or saving. These API observations are verified in UE 5.7; confirm names against the target engine when working in another version.

## Separate Module Loading From Script Execution

`importlib.reload(ModuleName)` requires `ModuleName` to have been bound by an earlier successful import. If the first import raised an exception, this follow-up error is only a secondary symptom:

```text
NameError: name 'ModuleName' is not defined
```

Retry a failed first load with a normal import. For an editor session that may or may not have loaded the module already, use one operation:

```python
import importlib
import sys

module_name = "MyAssetScript"
module = (
    importlib.reload(sys.modules[module_name])
    if module_name in sys.modules
    else importlib.import_module(module_name)
)
```

Do not run `import ModuleName` and immediately reload it in the same attempt when the module invokes `main()` at module scope: both operations execute the asset mutation. Preserve the first traceback because it contains the actual script failure; the later `NameError` does not.

## Resolve a Blueprint Generated Class

In UE 5.7, `unreal.Blueprint` exposes its generated class through a method:

```python
unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
generated_class = blueprint.generated_class()
```

It is not a reflected editor property. This call fails because `get_editor_property()` only reads reflected properties exposed on that object:

```python
blueprint.get_editor_property("generated_class")
```

The characteristic failure is:

```text
Blueprint: Failed to find property 'generated_class'
```

Compile before resolving the class. If `generated_class()` is still `None`, use `unreal.EditorAssetLibrary.load_blueprint_class(asset_path)` as a load fallback and fail explicitly if neither path returns a class.

## Access Components on the Blueprint CDO

After resolving the generated class, get its class default object and find inherited or native components by class:

```python
defaults = unreal.get_default_object(generated_class)
movement_class = getattr(unreal, "CharacterMovementComponent")
movement = defaults.get_component_by_class(movement_class)
if movement is None:
    raise RuntimeError("Blueprint CDO has no CharacterMovementComponent")
```

Do not assume a component is available as an editor property such as `character_movement`. A component can exist on the CDO without being exposed under that reflected property name.

## Edit Arrays Whose Struct Element Has No Python Constructor

A reflected C++ struct may be usable as an editor-property element without being exported as a named Python type. For example, a plain `USTRUCT()` without `BlueprintType` can produce this failure:

```text
AttributeError: module 'unreal' has no attribute 'SomeStruct'
```

When an owning UObject exposes a `TArray<SomeStruct>` editor property, obtain its reference-backed Unreal array, resize it to create native default elements, and edit those generic struct wrappers in place:

```python
items = owner.get_editor_property("items")
items.resize(len(specs))

for index, spec in enumerate(specs):
    item = items[index]
    item.set_editor_property("target_class", spec.target_class)
```

Do not invent `unreal.SomeStruct()` merely from the C++ name. Confirm the `USTRUCT` metadata and the actual `unreal` module first. This pattern depends on `get_editor_property()` returning a reference-backed Unreal array; read the array back from the owner and verify its length and important fields before saving.

## Write, Read Back, and Save

Treat an editor-property write as unverified until it round-trips:

```python
expected_speed = 600.0
movement.set_editor_property("max_swim_speed", expected_speed)
actual_speed = float(movement.get_editor_property("max_swim_speed"))
if abs(actual_speed - expected_speed) > 0.01:
    raise RuntimeError(f"MaxSwimSpeed write failed: {actual_speed}")

if not unreal.EditorAssetLibrary.save_asset(asset_path, only_if_is_dirty=False):
    raise RuntimeError(f"Failed to save asset: {asset_path}")
```

The binary success criterion is: the script reaches its completion log, the value reads back correctly, the save call succeeds, and the asset retains the value after reload. A created `.uasset` by itself is not proof that the script completed; an earlier mutation may persist even when a later statement raises.

## Triage Order

1. Capture the first traceback from the first import or execution.
2. Classify the failing expression as a method call, reflected property access, class lookup, CDO/component lookup, or save.
3. Compare with a working script in the same repository and engine version before guessing Python property names.
4. Apply the smallest API correction.
5. Compile before generated-class access; write, read back, save, and reload for validation.
