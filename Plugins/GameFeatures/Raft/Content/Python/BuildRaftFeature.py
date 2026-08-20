"""Build all Raft editor assets and integrate the test actor in one idempotent pass.

Run from Unreal Editor or UnrealEditor-Cmd after compiling RaftRuntime:

    py "<project>/Plugins/GameFeatures/Raft/Content/Python/BuildRaftFeature.py"
"""

from pathlib import Path
import runpy

import unreal


PROJECT_ROOT = Path(unreal.Paths.project_dir()).resolve()
RAFT_PYTHON = PROJECT_ROOT / "Plugins" / "GameFeatures" / "Raft" / "Content" / "Python"
OCEAN_PYTHON = (
    PROJECT_ROOT
    / "Plugins"
    / "GameFeatures"
    / "OceanAdventure"
    / "Content"
    / "Python"
)


def run_script(path):
    if not path.is_file():
        raise RuntimeError(f"Missing build script: {path}")
    unreal.log(f"[RaftFeatureBuilder] Running {path.name}")
    runpy.run_path(str(path), run_name="__main__")


def main():
    run_script(RAFT_PYTHON / "CreateRaftGameFeatureData.py")
    run_script(OCEAN_PYTHON / "CreateOceanAdventureExperience.py")
    run_script(RAFT_PYTHON / "CreateRaftTestActor.py")
    unreal.log("[RaftFeatureBuilder] Raft GameFeature build completed")


if __name__ == "__main__":
    main()
