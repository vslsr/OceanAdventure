# 建造系统

```
BuildMode 1
BuildSelect Raft.Piece.Prop.Campfire
```

## 定义

可用建筑模块由木筏的 Catalog 决定：
DA_BuildPieceCatalog_Raft.uasset`Plugins/GameFeatures/Raft/Content/Build/DA_BuildPieceCatalog_Raft.uasset`

```
BP_Raft_Default (蓝图)
└─ RaftDefinition ──────────────► DA_Raft_Default
   └─ BuildPieceCatalog ────────► DA_BuildPieceCatalog_Raft   ← 可用模块的清单在这
      └─ Pieces[0..n] ─────────► DA_BuildPiece_Raft_*         ← 每个模块的定义
```

它目前包含 9 个模块：
```
- Foundation Wood
- Deck
- Campfire
- Wall
- FireWindow
- Pontoon
- Thruster
- Rudder
- HeavyCannon
```

代码中定义：
```
BuildStructureComponent->SetPieceCatalog(RaftDefinition->GetBuildPieceCatalog());
```

## 怎么加一个新模块

两条路，选一条：

1. 编辑器里手加：在 Content/Build/Pieces 新建 RaftBuildPieceDefinition 资产，填好字段，然后打开目录追加到 Pieces 数组末尾。
2. 改脚本（推荐）：在 CreateRaftBuildPieceAssets.py 里照着 configure_campfire() 复制一份，加进 managed_pieces。脚本是幂等的，偏移和缩放能按包围盒自动算，比手填可靠。


## 调试命令

```
BuildMode 1
BuildSelect Raft.Piece.Prop.Campfire
```

完整执行流程如下：
```
控制台输入 "BuildSelect Raft.Piece.Prop.Campfire"
  │
  ├─ UCheatManager 反射派发到 UBuildCheats::BuildSelect(FString)
  │     UFUNCTION(Exec, BlueprintAuthorityOnly)
  │     客户端输入时经 ServerCheat 转到服务端执行
  │
  ├─ 1. GetBuildComponent()          找宿主
  │      脚下 MovementBase → 遍历兜底
  │
  ├─ 2. Structure->GetCatalog()      拿到 DA_BuildPieceCatalog_Raft
  │
  ├─ 3. FGameplayTag::RequestGameplayTag(FName("Raft.Piece.Prop.Campfire"), false)
  │      从 tag 注册表取；未注册 → 返回空 tag
  │
  ├─ 4. Catalog->FindByTag(Tag)      线性扫 Pieces[]，比对 Definition->PieceTag
  │      找不到 → 日志 "BuildSelect: unknown piece" 并 return
  │
  ├─ 5. Catalog->FindIndex(Definition, PieceIndex)   资产指针 → uint16 网络索引
  │
  └─ 6. Preview->SetSelectedPieceIndex(PieceIndex)
         清掉当前幽灵网格、重置材质状态与吸附格
         下一帧 UpdatePreview 用新索引从目录取件、重建幽灵
```

