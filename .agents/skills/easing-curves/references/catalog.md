# 曲线目录：31 条标准缓动的选型表

数据源是 `easings.net/easings.yml`（逐字取自上游，见 `easings.net/PROVENANCE.md`）。
本文只回答“挑哪条”；公式的可编译实现在 `../assets/OceanEasing.h`，引擎落点在 `unreal-mapping.md`。

## 一分钟选型

| 要做的事 | 用这条 |
|---|---|
| 摄像机推拉、跟随目标切换、视野变化 | `InOutSine` / `InOutQuad`（观感上“没有动画”，只是顺了一下） |
| UI 面板/HUD 弹出、道具面板打开 | `OutCubic`（默认）；要一点活泼用 `OutBack` |
| UI 面板关闭、元素退场 | `InCubic` / `InQuad`（退场比入场快，通常给 0.6× 时长） |
| 数值滚动（金币、耐久、建造进度） | `OutQuart`（前段快，尾巴长，读得清最终值） |
| 建造模块吸附到位、道具拾取吸入 | `OutBack`（到位时轻微过冲，手感“咔哒”一下） |
| 弹窗强调、获得稀有物 | `OutElastic`（别用在频繁触发的东西上，会腻） |
| 掉落物落地、船体撞击回弹 | `OutBounce` |
| 船舶加速/减速、浮力回正 | **一条都别用**——那是持续物理，见 `unreal-mapping.md`「曲线 vs 逼近」 |
| 匀速旋转、循环扫描、进度条按真实进度走 | `Linear` |

默认值：不知道选什么就用 `OutCubic`，时长 0.2–0.3s。90% 的 UI 动效到此为止。

## 十族手感

| 族 | 手感 | 何时用 |
|---|---|---|
| **Sine** | 最轻，只把生硬的匀速磨圆 | 摄像机、大面积位移、任何“不该被注意到”的运动 |
| **Quad** | 轻，标准起步 | UI 通用备选 |
| **Cubic** | 明确但不夸张，**本仓库的默认档** | UI 出入场、绝大多数 tween |
| **Quart** | 重，头尾对比强 | 数值滚动、长距离位移 |
| **Quint** | 很重，接近“先几乎不动再冲” | 少用，强调镜头 |
| **Expo** | 极端：起步/收尾几乎贴住端点 | 瞬间感的闪入闪出；**端点特判不能省**（见坑 3） |
| **Circ** | 圆弧，收尾刹得比 Quart 更死 | 需要“稳稳停住”的到位动作 |
| **Back** | 先反向蓄力再冲出，端点过冲 ±10% | 吸附到位、按钮按下回弹 |
| **Elastic** | 衰减振荡，来回穿越终点，过冲达 ±37% | 强调、奖励；高频触发会腻 |
| **Bounce** | 落地弹跳，只在终点侧回弹 | 掉落、撞击 |

三态的含义固定：`In` 慢入快出（加速，尾部最快），`Out` 快入慢出（减速，起步最快），
`InOut` 两头慢中间快。**入场用 Out，退场用 In，位移两端都可见时用 InOut**——记住这一条就够用了。

## 逐条数据

`cubic-bezier` 一列是站点给的等价贝塞尔（Elastic/Bounce 无法用单段三次贝塞尔表达，标 `—`）；
它对接的是 CSS / UMG 之外的工具（Figma、After Effects、Rive），UE 侧怎么用见 `unreal-mapping.md`。
`OceanEasing::` 一列是 `../assets/OceanEasing.h` 里的函数名（去掉了 `ease` 前缀）。

| 名称 | OceanEasing:: | cubic-bezier | 值域 |
|---|---|---|---|
| linear | `Linear` | `cubic-bezier(0, 0, 1, 1)` | [0, 1] |
| easeInSine | `InSine` | `cubic-bezier(0.12, 0, 0.39, 0)` | [0, 1] |
| easeOutSine | `OutSine` | `cubic-bezier(0.61, 1, 0.88, 1)` | [0, 1] |
| easeInOutSine | `InOutSine` | `cubic-bezier(0.37, 0, 0.63, 1)` | [0, 1] |
| easeInQuad | `InQuad` | `cubic-bezier(0.11, 0, 0.5, 0)` | [0, 1] |
| easeOutQuad | `OutQuad` | `cubic-bezier(0.5, 1, 0.89, 1)` | [0, 1] |
| easeInOutQuad | `InOutQuad` | `cubic-bezier(0.45, 0, 0.55, 1)` | [0, 1] |
| easeInCubic | `InCubic` | `cubic-bezier(0.32, 0, 0.67, 0)` | [0, 1] |
| easeOutCubic | `OutCubic` | `cubic-bezier(0.33, 1, 0.68, 1)` | [0, 1] |
| easeInOutCubic | `InOutCubic` | `cubic-bezier(0.65, 0, 0.35, 1)` | [0, 1] |
| easeInQuart | `InQuart` | `cubic-bezier(0.5, 0, 0.75, 0)` | [0, 1] |
| easeOutQuart | `OutQuart` | `cubic-bezier(0.25, 1, 0.5, 1)` | [0, 1] |
| easeInOutQuart | `InOutQuart` | `cubic-bezier(0.76, 0, 0.24, 1)` | [0, 1] |
| easeInQuint | `InQuint` | `cubic-bezier(0.64, 0, 0.78, 0)` | [0, 1] |
| easeOutQuint | `OutQuint` | `cubic-bezier(0.22, 1, 0.36, 1)` | [0, 1] |
| easeInOutQuint | `InOutQuint` | `cubic-bezier(0.83, 0, 0.17, 1)` | [0, 1] |
| easeInExpo | `InExpo` | `cubic-bezier(0.7, 0, 0.84, 0)` | [0, 1] |
| easeOutExpo | `OutExpo` | `cubic-bezier(0.16, 1, 0.3, 1)` | [0, 1] |
| easeInOutExpo | `InOutExpo` | `cubic-bezier(0.87, 0, 0.13, 1)` | [0, 1] |
| easeInCirc | `InCirc` | `cubic-bezier(0.55, 0, 1, 0.45)` | [0, 1] |
| easeOutCirc | `OutCirc` | `cubic-bezier(0, 0.55, 0.45, 1)` | [0, 1] |
| easeInOutCirc | `InOutCirc` | `cubic-bezier(0.85, 0, 0.15, 1)` | [0, 1] |
| easeInBack | `InBack` | `cubic-bezier(0.36, 0, 0.66, -0.56)` | **[-0.10, 1]** |
| easeOutBack | `OutBack` | `cubic-bezier(0.34, 1.56, 0.64, 1)` | **[0, 1.10]** |
| easeInOutBack | `InOutBack` | `cubic-bezier(0.68, -0.6, 0.32, 1.6)` | **[-0.10, 1.10]** |
| easeInElastic | `InElastic` | — | **[-0.37, 1]** |
| easeOutElastic | `OutElastic` | — | **[0, 1.37]** |
| easeInOutElastic | `InOutElastic` | — | **[-0.12, 1.12]** |
| easeInBounce | `InBounce` | — | [0, 1] |
| easeOutBounce | `OutBounce` | — | [0, 1] |
| easeInOutBounce | `InOutBounce` | — | [0, 1] |

值域列是按 10000 个采样点实测的极值（`OceanEasing.h` 与上游 TS 实现在 0…1 上逐点比对过，
误差 < 1e-4）。**加粗那六行会冲出 [0,1]**，喂给任何会夹取或不能为负的量之前先读坑 1。

## 调参而不是换曲线

想要“同一条曲线但更狠/更收敛”，改常数比换族更可控，改完记得留一行注释说明改了哪个常数：

- **Back 的过冲量**：`C1 = 1.70158`（过冲约 10%）。`C1 = 3.0` 约 17%，`C1 = 0.8` 约 5%。
- **Elastic 的振荡快慢**：`C4 = 2π/3`。分母变大 → 周期变长、晃得少；`Pow(2, -10 * X)` 里的
  `-10` 控制衰减，改成 `-6` 会晃更久。
- **幂次族**：Quad→Quint 就是指数 2→5，要 2.5 这种中间值直接用 `FMath::Pow(X, 2.5f)`，
  不必新增函数。
