"""Migrate WildOmission terrain/water assets without resaving them in UE 5.7.

Run this file with the regular Windows Python launcher, not the Unreal Python console:

    py MigrateWildOmissionEnvironment.py

The source assets were authored in UE 5.3. One texture crashes UE 5.7's Interchange
save path, so this orchestrator creates an isolated UE 5.3 project, performs all package
renames there, installs the resulting packages into OceanAdventure, and finally runs a
read-only UE 5.7 validation commandlet.
"""

from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import subprocess


SCRIPT_PATH = Path(__file__).resolve()
PROJECT_ROOT = SCRIPT_PATH.parents[5]
TARGET_PROJECT = PROJECT_ROOT / "LyraTemplate.uproject"
TARGET_CONTENT = (
    PROJECT_ROOT
    / "Plugins"
    / "GameFeatures"
    / "OceanAdventure"
    / "Content"
    / "Environment"
    / "WildOmission"
)
HOST_ROOT = PROJECT_ROOT / "Saved" / "WildOmissionMigration53"
HOST_PROJECT = HOST_ROOT / "WildOmissionMigration.uproject"
HOST_PLUGIN = HOST_ROOT / "Plugins" / "OceanAdventure"
UNREAL_SCRIPT = SCRIPT_PATH.with_name("MigrateWildOmissionEnvironment_Unreal.py")

SOURCE_PROJECT_ROOT = Path(
    os.environ.get("WILD_OMISSION_ROOT", r"E:\WildOmission-1.0.1-beta")
)
UE53_EDITOR = Path(
    os.environ.get(
        "UE53_EDITOR",
        r"E:\Program Files\Epic Games\UE_5.3\Engine\Binaries\Win64\UnrealEditor-Cmd.exe",
    )
)
UE57_EDITOR = Path(
    os.environ.get(
        "UE57_EDITOR",
        r"E:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe",
    )
)

SOURCE_PACKAGES = [
    "/Game/WildOmissionCore/Art/Common/T_Fingerprints_01",
    "/Game/WildOmissionCore/Art/Common/T_Fingerprints_02",
    "/Game/WildOmissionCore/Art/Common/T_FoliageNoise",
    "/Game/WildOmissionCore/Art/Common/T_Metal_Scratched_01",
    "/Game/WildOmissionCore/Art/Common/T_Metal_Scratched_02",
    "/Game/WildOmissionCore/Art/MaterialFunctions/MF_DistanceFieldMask",
    "/Game/WildOmissionCore/Art/MaterialFunctions/MF_GreenColor",
    "/Game/WildOmissionCore/Art/MaterialFunctions/MF_PlasticSpecular",
    "/Game/WildOmissionCore/Art/MaterialFunctions/MF_ReactiveRipple",
    "/Game/WildOmissionCore/Art/MaterialFunctions/MF_WaterWave",
    "/Game/WildOmissionCore/Art/MaterialFunctions/MF_WhiteColor",
    "/Game/WildOmissionCore/Art/MaterialFunctions/MF_WorldPositionShading",
    "/Game/WildOmissionCore/Art/Environment/M_Water",
    "/Game/WorldGeneration/M_Terrain",
    "/Game/WorldGeneration/SM_Water",
]
PACKAGE_EXTENSIONS = (".uasset", ".uexp", ".ubulk", ".uptnl")


def require_file(path: Path, description: str) -> None:
    if not path.is_file():
        raise RuntimeError(f"Missing {description}: {path}")


def write_json(path: Path, value: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2), encoding="utf-8")


def create_host_project() -> None:
    expected_parent = (PROJECT_ROOT / "Saved").resolve()
    if HOST_ROOT.resolve().parent != expected_parent:
        raise RuntimeError(f"Refusing to replace unexpected host path: {HOST_ROOT}")

    if HOST_ROOT.exists():
        shutil.rmtree(HOST_ROOT)

    write_json(
        HOST_PROJECT,
        {
            "FileVersion": 3,
            "EngineAssociation": "5.3",
            "Category": "",
            "Description": "Temporary WildOmission asset migration host",
            "Plugins": [
                {"Name": "PythonScriptPlugin", "Enabled": True},
                {"Name": "EditorScriptingUtilities", "Enabled": True},
                {"Name": "OceanAdventure", "Enabled": True},
            ],
        },
    )
    write_json(
        HOST_PLUGIN / "OceanAdventure.uplugin",
        {
            "FileVersion": 3,
            "Version": 1,
            "VersionName": "1.0",
            "FriendlyName": "OceanAdventure migration mount",
            "CanContainContent": True,
            "Installed": False,
        },
    )


def run_commandlet(editor: Path, project: Path, mode: str) -> None:
    environment = os.environ.copy()
    environment["WILD_OMISSION_ROOT"] = str(SOURCE_PROJECT_ROOT)
    environment["WILD_OMISSION_MIGRATION_MODE"] = mode
    command = [
        str(editor),
        str(project),
        "-run=pythonscript",
        f"-script={UNREAL_SCRIPT}",
        "-unattended",
        "-nop4",
        "-nosplash",
        "-nullrhi",
        "-stdout",
        "-FullStdOutLogOutput",
    ]
    print(f"Running UE migration phase '{mode}' with {editor}", flush=True)
    subprocess.run(command, env=environment, check=True)


def remove_failed_ue57_staging() -> None:
    project_content = PROJECT_ROOT / "Content"
    for package in SOURCE_PACKAGES:
        relative = Path(*package.removeprefix("/Game/").split("/"))
        file_base = project_content / relative
        for extension in PACKAGE_EXTENSIONS:
            candidate = file_base.with_suffix(extension)
            if candidate.is_file():
                candidate.unlink()

    for relative_root in ("WildOmissionCore", "WorldGeneration"):
        root = project_content / relative_root
        if root.is_dir():
            for directory in sorted(root.rglob("*"), reverse=True):
                if directory.is_dir() and not any(directory.iterdir()):
                    directory.rmdir()
            if not any(root.iterdir()):
                root.rmdir()


def install_prepared_assets() -> None:
    prepared = HOST_PLUGIN / "Content" / "Environment" / "WildOmission"
    if len(list(prepared.rglob("*.uasset"))) != len(SOURCE_PACKAGES):
        raise RuntimeError(f"Prepared asset set is incomplete: {prepared}")

    if TARGET_CONTENT.exists():
        shutil.rmtree(TARGET_CONTENT)
    TARGET_CONTENT.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(prepared, TARGET_CONTENT)
    remove_failed_ue57_staging()
    print(f"Installed {len(SOURCE_PACKAGES)} assets under {TARGET_CONTENT}", flush=True)


def main() -> None:
    for path, description in (
        (TARGET_PROJECT, "target project"),
        (SOURCE_PROJECT_ROOT / "WildOmission.uproject", "WildOmission project"),
        (UE53_EDITOR, "UE 5.3 commandlet"),
        (UE57_EDITOR, "UE 5.7 commandlet"),
        (UNREAL_SCRIPT, "Unreal migration script"),
    ):
        require_file(path, description)

    create_host_project()
    run_commandlet(UE53_EDITOR, HOST_PROJECT, "prepare")
    install_prepared_assets()
    run_commandlet(UE57_EDITOR, TARGET_PROJECT, "validate")
    print("WildOmission migration completed and validated.")


if __name__ == "__main__":
    main()
