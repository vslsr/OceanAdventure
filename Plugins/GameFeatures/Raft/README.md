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
