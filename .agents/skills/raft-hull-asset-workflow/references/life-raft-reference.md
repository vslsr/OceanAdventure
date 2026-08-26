# LifeRaft 已验证参考轨迹

此参考记录用于复用已经跑通的资产形状和脚本边界，不要求其它船体复制 LifeRaft 的颜色、浮力数值或不可建造语义。

## 最终资产族

```text
blender/script/python/SM_LifeRaft.py
blender/models/SM_LifeRaft.blend
blender/models/SM_LifeRaft.fbx

/Raft/Vehicles/LifeRaft/
├─ SM_LifeRaft
├─ DA_Raft_LifeRaft
├─ BP_Raft_LifeRaft
└─ M_LifeRaft_*

Plugins/GameFeatures/Raft/Content/Python/CreateRaftLifeRaftAssets.py
```

主木筏继续位于 `/Raft/Vehicles/Raft`。LifeRaft 没有复用主木筏目录，也没有复用 `SM_Raft`。

## Blender 实现方式

- 可见轮廓使用小型橙色充气环、蓝色底板、反光把手、折叠篷包和救援旗，与木质主筏形成稳定视觉差异。
- 可见体量约为 `150 × 120 × 90 cm`，但原点和 UCX 仍服从 `200 × 200 × 150 cm` 建筑模块。
- 脚本拥有 `SM_LifeRaft_Generated` 与 `SM_LifeRaft_Collision_Generated` 两个集合，重跑只替换它们。
- 输出固定进入工程根的 `blender/models`，并同时保存 `.blend` 与导出 `.fbx`。

## UE 实现方式

- `CreateRaftNavalAssets.py` 中的 `configure_life_raft()` 持有共享的幂等导入和 DA/BP 配置实现。
- `CreateRaftLifeRaftAssets.py` 是必须保留的独立入口：它以非 main 名称加载共享文件，只调用 `configure_life_raft()`，不执行舰炮、建造件、舵台或组件生成。
- `DA_Raft_LifeRaft` 使用 `SM_LifeRaft`，碰撞半尺寸为 `(100,100,75) cm`，浮力采样点按小筏轮廓收紧。
- `build_piece_catalog=None` 是不可建造的唯一真值；不要在 Blueprint 或 Ability 中再维护第二套“禁止建造”开关。
- `BP_Raft_LifeRaft` 继承 `ARaftActor`，CDO 的 `raft_definition` 指向 LifeRaft DA。
- 独立构建脚本修复并断言 BP CDO 根组件为 `Movable`；可见网格是否 Movable 不能替代 Actor 根组件约束。
- `DefaultGame.ini` 的 `LifeRaftClass` 指向 `/Raft/Vehicles/LifeRaft/BP_Raft_LifeRaft.BP_Raft_LifeRaft_C`。

## 已发生故障一：Blender 找不到工程根

现象：

```text
RuntimeError: Could not locate OceanAdventure project root
```

真实条件是 Blender Text Editor 把脚本暴露为 `\SM_LifeRaft.py`，同时 `.blend` 尚未保存，`__file__`、cwd 和 `bpy.data.filepath` 都不能提供工程锚点。

修复：保留环境变量覆盖和结构验证；动态锚点失败后使用经过验证的标准工作区回退。修改磁盘文件后重新加载 Blender 文本块。

验证：伪路径、OceanAdventure cwd、LyraStarterGame cwd 三种测试均解析到 `C:\EpicWkspc\OceanAdventure`。

## 已发生故障二：救生筏被舰炮前置条件阻断

现象：

```text
RuntimeError: Missing /NavalCore/Naval/BP_Naval_Cannon
```

根因是运行了完整 `CreateRaftNavalAssets.py`；它先配置甲板炮，再到救生筏。LifeRaft 本身不依赖舰炮。

修复：新增并运行 `CreateRaftLifeRaftAssets.py`，只调用 LifeRaft 配置函数。完整 Naval 脚本继续严格要求共享舰炮，不通过跳过错误来掩盖不完整的 Naval 资产。

最终证据：用户在 UE 中运行独立入口后确认“创建成功”。

## 已发生故障三：根组件为 Static，UE 拒绝移动

现象是船体资产已经生成，但 UE 在浮力或其它移动路径调用 Actor 移动时拒绝执行，因为 Blueprint CDO 的根组件 Mobility 为 `Static`。

修复位于共享 LifeRaft 配置函数：设置 `raft_definition` 时同时把 Actor 根组件设为 `ComponentMobility.MOVABLE`；Blueprint 编译可能替换 GeneratedClass，因此编译后重新获取 CDO，再次修复和断言 Mobility，最后保存。

判据不是可见 StaticMesh 可移动，而是 `BP_Raft_LifeRaft` 编译后的 Actor 根组件 Mobility 读回为 `Movable`，并能在 PIE 中随浮力更新位置。

## 路径迁移注意事项

最初脚本把 LifeRaft 的 SM/DA/BP 指向 `/Raft/Vehicles/Raft`，而实际网格和材质被放入 `/Raft/Vehicles/LifeRaft`。修复时使用单一 `LIFE_RAFT_ROOT` 派生全部资产路径，并同步 `DefaultGame.ini`。

旧 `DA_Raft_LifeRaft` 若仍在旧目录，不从文件系统强删；在新路径验证完成后用 UE Content Browser 删除并 Fix Up Redirectors。
