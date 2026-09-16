# 落到 UE5.7 / Lyra：用哪一层、哪些坑会咬人

选曲线看 `catalog.md`，这里只讲“在这个工程里怎么写”。

## 先分清：缓动曲线 vs 指数逼近

两者都让运动变软，但语义完全不同，混用是本仓库最容易出的错：

| | 缓动曲线（本技能） | 指数逼近 `FInterpTo` / `VInterpTo` / `RInterpTo` |
|---|---|---|
| 需要什么 | **已知起点、终点、总时长**，自己维护 Elapsed | 只要“当前值 + 目标值 + 速度”，目标可以每帧变 |
| 什么时候结束 | Alpha 到 1 就精确到位 | 渐近，永远差一点（`FInterpConstantTo` 才会真正到达） |
| 帧率 | 与帧率无关（按时间归一化） | 引擎实现已按 DeltaTime 处理，但**结果仍随帧率轻微漂移** |
| 适合 | UI 出入场、镜头切换、吸附到位、数值滚动、一次性表演 | 持续跟随、物理阻尼、玩家输入驱动的连续运动 |

本仓库的既有用法就是这条分界线：`NavalMovementComponent`（船体速度/转向阻尼）和
`RaftBuoyancyComponent`（浮力回正）用的是 `FInterpTo` / `FInterpConstantTo`——那是持续状态，
**不要**改成缓动曲线；而 `ULyraCameraMode::SetBlendWeight` 是定长混合，用的才是缓动。

判据一句话：**说得出“这个动作持续 0.25 秒”就用曲线，说不出就用 InterpTo。**

## 三个层，各自怎么写

### 1. C++ 运行时：`OceanEasing.h` + 自己数时间

`FMath` 只自带 Sine / 幂次（`InterpEaseIn/Out/InOut` 带指数参数）/ Expo / Circular 四族，
**没有 Back、Elastic、Bounce**，也没有 Quad…Quint 的具名版本。要这三族或想让代码里写的名字和
easings.net 对得上，就用 `../assets/OceanEasing.h`（把它复制进目标模块的 `Public/`；
放哪个模块按 `AGENTS.md` 的 GameFeature 归属规则决定，跨插件共用才下沉到通用插件）。

```cpp
#include "OceanEasing.h"

// 组件上：float Elapsed = 0.f; const float Duration = 0.25f;
void UMyComponent::TickComponent(float DeltaTime, ...)
{
    Elapsed += DeltaTime;
    const float Alpha = OceanEasing::Normalize(Elapsed, Duration);   // 只夹取进度
    const float Curved = OceanEasing::OutBack(Alpha);                // 不夹取输出
    SetRelativeLocation(FMath::Lerp(StartLocation, TargetLocation, Curved));

    if (Alpha >= 1.f) { /* 收尾：显式写入终值，别指望曲线的浮点尾数 */ }
}
```

已有 `FMath` 能覆盖的就别重复造：`FMath::InterpEaseInOut(0.f, 1.f, Alpha, 2.f)` 等价于
`InOutQuad`，指数 3/4/5 依次对应 Cubic/Quart/Quint。**注意 `ULyraCameraMode` 传进去的是
`1 / BlendExponent`**——那是它自己的反解语义，照抄那行代码到别处会得到一条完全不同的曲线。

### 2. Blueprint / 曲线资产：`Ease` 节点或 `UCurveFloat`

- **Blueprint `Ease` 节点**（`UKismetMathLibrary::Ease`，参数 `EEasingFunc`）覆盖面和 `FMath` 一样：
  Linear / Step / Sinusoidal / EaseIn-Out(幂次，配 `BlendExp`) / Expo / Circular。Back、Elastic、
  Bounce 同样没有——需要时在模块里包一个 `UBlueprintFunctionLibrary` 暴露 `OceanEasing`，
  别在蓝图里用几个节点手搓公式（改不动也没人看得懂）。具体枚举项名以所用引擎版本的
  `Kismet/KismetMathLibrary.h`、`Math/UnrealMathUtility.h` 为准（本仓库不含引擎源码）。
- **`UCurveFloat` 资产 + `UTimelineComponent`**：策划要能自己调手感时用这个。把曲线做成资产，
  C++ 只读 `GetFloatValue(Alpha)`。资产归属仍按 GameFeature 走，别塞进 `/Game`。
- **cubic-bezier 值不能直接填进 UE**。`catalog.md` 里那列是给 Figma / AE / Rive 用的；
  在 `UCurveFloat` 里复刻时用关键帧 + 切线逼近，或者干脆在 C++ 里用公式，别试图“翻译”控制点。

### 3. UMG：动画资产优先，代码兜底

Widget Animation 的关键帧自带插值模式，UI 里绝大多数出入场用它就够了。
只有当曲线要跟随运行时数据（进度条读真实建造进度、数值滚动读服务端下发值）时，
才在 Widget 的 `NativeTick` 里用 `OceanEasing` 算，然后写进 Slot / RenderTransform。

## 三条会咬人的坑

### 坑 1：Back / Elastic 的过冲被下游夹掉或炸掉

`OutBack` 最高冲到 1.10，`OutElastic` 冲到 1.37，`InElastic` 最低到 -0.37（实测值见 `catalog.md`）。
把这些值直接喂给以下任何一个都会出事，而且**多半不报错，只是手感被削平**：

- `FMath::Clamp(..., 0.f, 1.f)` 或任何自带夹取的 setter → 过冲段被压成一条直线，白加了曲线；
- **缩放**：`Lerp(0, 1, InElastic(x))` 会给出负缩放，模型被翻面；
- **颜色 / Alpha**：分量出 [0,1] 被材质夹取，收尾闪一下；
- **半径 / 尺寸 / 时长**：负值传进画圆、Niagara 尺寸、定时器会直接抛错或静默失效。

对策：过冲曲线只用在**位置、旋转、以 1 为中心的缩放**（`Lerp(0.8f, 1.0f, OutBack(x))` 这种两端都安全的区间）上；
其余场景改用 `OutCubic` / `OutQuart`。

### 坑 2：把 DeltaTime 当成了 Alpha

```cpp
// ✗ 帧率相关：60fps 和 30fps 走出来是两种速度，且永远到不了终点
Location = FMath::Lerp(Location, Target, OceanEasing::OutCubic(DeltaTime * Speed));

// ✓ 要么归一化时间走曲线，要么老实用 FInterpTo（见上面的分界线）
Elapsed += DeltaTime;
Location = FMath::Lerp(StartLocation, Target, OceanEasing::OutCubic(OceanEasing::Normalize(Elapsed, Duration)));
```

缓动函数的输入**只能**是「已走时间 / 总时长」。逐帧对当前值再做一次缓动，等于把曲线套在
指数逼近上——两层都软，结果是“怎么调都黏糊糊”。

### 坑 3：Expo / Elastic 的端点特判不能省

`FMath::Pow(2.f, -10.f * 0.f) == 1`。删掉 `X == 0` 那行，`OutExpo(0)` 会返回 0 而
`InExpo(0)` 返回 1 —— 动画第一帧直接闪到终点。`OceanEasing.h` 里六处 `if (X == 0.f)` / `if (X == 1.f)`
是公式的一部分，不是防御性代码，重写或精简时别顺手清掉。

同理，`InBounce` / `InOutBounce` 都靠 `OutBounce` 反推，分段里的 `X -= ...` 作用在**形参副本**上；
抄成修改调用方变量的写法会让后续分段全错。

## 验证

引擎不在仓库里，编译验证只能在有 UE 的机器上做。纯数学部分可以离线核对：把
`assets/OceanEasing.h` 和一份最小 `FMath` 桩一起用 `g++ -std=c++17` 编译，采样 0…1
与 `easings.net/easingsFunctions.ts`（`node` 直接跑）逐点比对，误差应 < 1e-4。
这套比对在该文件落库时跑过一次，31 条 × 21 个采样点全过；改动公式后重跑同样的比对。

表现层的验收仍然是**在 PIE 里看**：看起点有没有闪跳、终点有没有停住、过冲有没有被夹平。
