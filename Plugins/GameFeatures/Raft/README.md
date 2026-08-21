# Raft GameFeature

`RaftRuntime` owns the default raft definition, replicated moving-platform actor, and
server-only kinematic buoyancy. `OceanCoreRuntime` remains the source of deterministic
water height/normal/velocity samples; Raft depends on OceanCore, never on OceanAdventure.

Runtime flow:

1. `BP_Experience_Ocean` activates the registered `Raft` GameFeature.
2. A placed or spawned `ARaftActor` ticks buoyancy on authority only.
3. Four pontoon samples query the active `UOceanWorldManagerComponent`/`AOceanChunkActor`.
4. The server updates the stable deck collision transform.
5. Standard `AActor::ReplicatedMovement` sends that transform to clients, where Lyra
   characters can use `DeckCollision` as their CharacterMovement movement base.

Editor asset generation:

```text
BuildRaftFeature.py            -> runs the complete sequence below
CreateRaftGameFeatureData.py  -> /Raft/Raft
CreateRaftTestActor.py        -> imports SM_Raft, creates Definition/Blueprint,
                                 places "Raft Test Actor" in L_OceanChunkTest
ValidateRaftFeature.py        -> read-only asset, replication, Experience, and map checks
```

The scripts are idempotent. Driving, helm interaction, input, and GAS abilities are
deliberately outside this first implementation.

## Creative building MVP

`BuildingCoreRuntime` supplies the replicated grid structure, ISM collision, and the
console-driven local preview. Raft owns the wooden foundation Definition/Catalog and
implements `IBuildStructureHost`.

In PIE, open the console and run:

```text
BuildMode 1
```

The mouse moves the preview. A supported empty cell snaps to the raft grid; left-click
places it through the server-authoritative component. Unsupported, occupied, or distant
positions remain unsnapped and use a red invalid material without shaking. Clicking there
triggers one short shader World Position Offset shake; the component transform stays fixed.
Right-click or Escape leaves the mode.
`BuildCreative 1` is an alias. Useful P0 commands also include `BuildDump`, `BuildClear`,
`BuildFill X Y`, and `BuildDebug 1`.

Asset generation and read-only validation:

```text
py "<project>/Plugins/GameFeatures/Raft/Content/Python/CreateRaftBuildPieceAssets.py"
py "<project>/Plugins/GameFeatures/Raft/Content/Python/ValidateRaftBuildMvp.py"
```
