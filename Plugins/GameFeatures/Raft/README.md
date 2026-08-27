# Raft GameFeature

`RaftRuntime` owns the water-vehicle base, expandable raft actor, and
server-only kinematic buoyancy. `OceanCoreRuntime` remains the source of deterministic
water height/normal/velocity samples; Raft depends on OceanCore, never on OceanAdventure.

Runtime flow:

1. `BP_Experience_Ocean` activates the registered `Raft` GameFeature.
2. A placed or spawned `ARaftVesselActor` ticks buoyancy on authority only.
3. Four pontoon samples query the active `UOceanWorldManagerComponent`/`AOceanChunkActor`.
4. The server updates the stable deck collision transform.
5. While `UNavalMovementComponent` is injected, it replaces snapping `AActor::ReplicatedMovement`
   with a replicated pose/velocity sample; clients interpolate that sample every frame, so Lyra
   characters can use the smoothly moving `DeckCollision` as their CharacterMovement movement base.

Editor asset generation:

```text
BuildRaftFeature.py            -> runs the core Definition/test/build-piece pipeline
CreateRaftGameFeatureData.py  -> /Raft/Raft
CreateRaftTestActor.py        -> imports SM_Raft, creates Definition/Blueprint,
                                 places "Raft Test Actor" in L_OceanChunkTest
CreateRaftLifeRaftAssets.py   -> focused emergency-raft pass; no cannon dependency
CreateRaftNavalAssets.py      -> naval pass; creates the fixed helm build piece and reparents the
                                 /Raft/Vehicles/LifeRaft family to the non-buildable vessel base
ValidateRaftFeature.py        -> read-only asset, replication, Experience, and map checks
```

The scripts are idempotent. Input and GAS abilities remain in OceanAdventure; hull/build
ownership remains in Raft.

The two hull definitions intentionally have different responsibilities:

```text
DA_Raft_Default
└─ BP_Raft_Default       ARaftActor: buildable main raft with append-only build catalog

DA_Raft_LifeRaft
└─ BP_Raft_LifeRaft      ARaftVesselActor: direct E driving, no build interface/components
```

All three emergency-hull assets (`SM_LifeRaft`, `DA_Raft_LifeRaft`, and
`BP_Raft_LifeRaft`) live under `/Raft/Vehicles/LifeRaft`. The main wooden hull remains under
`/Raft/Vehicles/Raft`.

First execute `blender/script/python/SM_LifeRaft.py` in Blender. It regenerates
`blender/models/SM_LifeRaft.blend` and `SM_LifeRaft.fbx` without touching unrelated objects.
Then run `CreateRaftLifeRaftAssets.py` for the life raft alone. The full
`CreateRaftNavalAssets.py` pass additionally requires `/NavalCore/Naval/BP_Naval_Cannon`.

The main raft does not receive a helm automatically. In a server console, select the fixed
helm with `BuildSelect Raft.Piece.Prop.Helm` and place it with `BuildPlace X Y Level`.
The life raft is the intentional exception: press `E` near its hull to drive it directly; no
helm Actor is spawned or built.

## Creative building MVP

`BuildingCoreRuntime` supplies the replicated grid structure, ISM collision, and the
console-driven local preview. Raft owns the build-piece Definition/Catalog and implements
`IBuildStructureHost`. Building/hull modules use a fixed `200 × 200 cm` horizontal cell and
`150 cm` level height (`200 × 200 × 150 cm` total), independent of the dynamically expanded
`DeckCollision`. The original raft is cell `(0,0)`; each placed piece is the same unscaled
`VisualMesh`, snapped one 200 cm cell away and attached to the same replicated raft actor so
the connected sections cannot drift apart.

Before PIE, generate the gameplay-owned input, GAS, and pawn-component assets after any
experience-generation script that may rewrite `OceanAdventure.uasset`:

```text
py "<project>/Plugins/GameFeatures/OceanAdventure/Content/Python/CreateOceanAdventureBuildAssets.py"
```

Restart the editor, stand on or near a raft, and press `B`. The mouse then moves a full-raft
preview. A supported empty cell snaps to one of the four sides; left-click places it through
the server-authoritative TargetData path. Press `X` to remove and `B` again to leave build mode.
Unsupported, occupied, or distant
positions remain unsnapped and use a red invalid material without shaking. Clicking there
triggers one short shader World Position Offset shake; the component transform stays fixed.

`BuildMode 1` / `BuildCreative 1` are ghost-only diagnostics. They intentionally do not grant
`Status.Build.Active`, so left-click placement is not expected in that diagnostic mode. Useful
server-side diagnostics also include `BuildDump`, `BuildClear`, `BuildFill X Y`, and `BuildDebug 1`.

Asset generation and read-only validation:

```text
py "<project>/Plugins/GameFeatures/Raft/Content/Python/CreateRaftBuildPieceAssets.py"
py "<project>/Plugins/GameFeatures/Raft/Content/Python/ValidateRaftBuildMvp.py"
```
