---
name: easing-curves
description: 为 OceanAdventure/Lyra 选择并落地缓动曲线（easing / 缓冲动画 / 动效曲线），收录 easings.net 那 31 条标准曲线的公式、cubic-bezier 值、手感说明，以及可直接进工程的 UE C++ 实现。用户说“加个缓动/缓冲动画、动画太生硬、匀速太假、弹一下/回弹/过冲、慢入快出、easeOutBack 是多少、cubic-bezier 填什么、UI 弹出/退场动效、数值滚动、镜头推拉曲线”，或排查“起点闪跳、过冲被夹平、动画帧率不一致、缩放变负”时使用。只管“用哪条曲线、公式是什么、写在 UE 哪一层”；持续物理阻尼与跟随（FInterpTo/浮力/船体操控）不属于本技能，骨骼动画、Sequencer 剪辑、材质动画也不属于。
license: Apache-2.0
---

# 缓动曲线（缓冲动画）

任何“某个东西在 N 秒内从 A 变到 B”的表现——UI 出入场、镜头推拉、模块吸附、数值滚动、
拾取吸入——都要过一条曲线。匀速（`Linear`）只适合真正匀速的东西（旋转、按真实进度走的进度条）。

本技能的三份材料：

| 文件 | 回答什么 |
|---|---|
| `references/catalog.md` | **选哪条**：一分钟选型表、十族手感、31 条逐条数据（cubic-bezier + 实测值域） |
| `references/unreal-mapping.md` | **怎么写**：曲线 vs `FInterpTo` 的分界线、C++/蓝图/UMG 三层落点、三条会咬人的坑 |
| `assets/OceanEasing.h` | **拿来用**：31 条曲线的 UE 实现，无 UHT 宏，复制进目标模块即可编译 |
| `references/easings.net/` | 上游参考源码副本（`easings.yml` + `easingsFunctions.ts` + LICENSE），只作查阅 |

## 工作流

1. **先判断是不是缓动问题。** 说得出“这个动作持续 X 秒、从哪到哪”才是；只有“当前值追着目标值跑”
   （船体速度、浮力回正、摄像机跟随）是指数逼近，用 `FMath::FInterpTo` 一族，不要套曲线。
   判据和本仓库的既有先例见 `references/unreal-mapping.md` 开头那张表。
2. **查 `references/catalog.md` 选曲线。** 从一分钟选型表落到具体一条；拿不准就 `OutCubic` + 0.2–0.3s。
   入场 `Out`、退场 `In`、两端都可见 `InOut`。
3. **选落点层**（`references/unreal-mapping.md` 第二节）：
   - 策划要自己调手感 → `UCurveFloat` 资产 + Timeline；
   - 纯 UI 出入场 → Widget Animation 关键帧；
   - 曲线要跟运行时数据走、或需要 Back/Elastic/Bounce → C++，用 `assets/OceanEasing.h`。
     `FMath` 自带的 Sine/幂次/Expo/Circular 能覆盖时优先用引擎的，别重复造。
4. **写之前先读那三条坑**（同文件末尾）。过冲曲线喂给缩放/颜色/半径、把 `DeltaTime` 当 Alpha、
   删掉 Expo/Elastic 的端点特判——这三样都不会报错，只会让动效悄悄变味或第一帧闪跳。
5. **验证**：数学部分离线比对（`g++` 编译 `OceanEasing.h` + `node` 跑上游 TS，误差 < 1e-4，
   方法见 `unreal-mapping.md` 末节）；表现部分必须在 PIE 里看起点、终点、过冲。

## 硬性约束

- **`assets/OceanEasing.h` 落地时要挑对模块**：按 `AGENTS.md` 的 GameFeature 归属规则，
  只有一个 GameFeature 用就放进那个 GameFeature 的模块，跨插件共用才下沉到通用插件的 `Public/`。
  不要放进项目 `/Game` 或根 `Source/` 图省事。
- **`references/easings.net/` 是 GPL-3.0 的上游副本，不得 `#include`、不得把其中代码复制进
  `Source/` 或 `Plugins/`**。可落地的是公式本身（经典公开数学定义），`OceanEasing.h` 就是按公式
  独立写的实现。细节见 `references/easings.net/PROVENANCE.md`。
- 改曲线常数（Back 的过冲量、Elastic 的衰减）时在那一行留注释说明改了什么，
  否则下一个人只会看到一串魔数（可调常数清单在 `catalog.md` 末节）。

## 边界

- **持续跟随/物理阻尼**（`FInterpTo`、浮力、船体操控、摄像机跟随）：不是缓动曲线问题，
  见 `references/unreal-mapping.md` 第一节的分界线，别把它们改写成定长 tween。
- **UI 控件本身的搭建与布局**：改用 `ue5-ui-umg-slate`；本技能只管其中的时间曲线。
- **骨骼动画、Montage、Sequencer 剪辑、材质/Niagara 内部的曲线**：各归各的系统，
  本技能的公式可以借用，但不负责那些资产的工作流。
- 不新增、不修改任何引擎或工程源码：本技能只提供选型、公式和一份待复制的头文件；
  真要把 `OceanEasing.h` 落进某个模块，按用户当次的任务范围做，不擅自铺开。
