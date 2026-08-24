# 鼠标模式：Lyra 的正确实现方式

> 结论先行：**鼠标模式不是 PlayerController 上的一个 bool，而是"当前激活的 UI 层声明的输入配置"。**
> 谁在最上层，谁说了算；层被弹出，配置自动回退。任何直接写 `SetShowMouseCursor()` 的代码
> 都在和 CommonUI 抢方向盘，迟早被覆盖。

---

## 1. 机制链路

```
ULyraActivatableWidget（你的 HUD / 建造界面 / 菜单）
   │  InputConfig            = Default / Game / GameAndMenu / Menu
   │  GameMouseCaptureMode   = CapturePermanently / NoCapture / ...
   │  bIgnoreLookInput       = true/false
   ↓ GetDesiredInputConfig()  →  FUIInputConfig
UCommonUIActionRouterBase（CommonUI 的 ActionRouter，监听 widget 栈变化）
   ↓ 取"栈顶那个有声明的 widget"的配置并应用
APlayerController
   bShowMouseCursor / SetInputMode / MouseCaptureMode / IgnoreLookInput
```

现成的枚举语义（`Source/LyraGame/UI/LyraActivatableWidget.cpp`）：

| `ELyraWidgetInputMode` | `ECommonInputMode` | 鼠标捕获 | 典型用途 |
| --- | --- | --- | --- |
| `Default` | 不声明，沿用下层 | — | 纯展示的血条、小地图 |
| `Game` | Game | 用 `GameMouseCaptureMode` | Lyra FPS 默认 HUD（`CapturePermanently` → 光标隐藏并锁定） |
| `GameAndMenu` | All | 用 `GameMouseCaptureMode` | **TopDown / 建造模式**：既要点 UI，也要游戏输入 |
| `Menu` | Menu | 强制 `NoCapture` | 全屏菜单、设置、背包 |

关键点：`GameAndMenu + NoCapture` 就是"鼠标可见、可点 UI、同时 WASD 仍进入游戏"，
这正是 TopDown 生存建造需要的模式。鼠标只负责角色朝向，不再触发点击移动。

---

## 2. 三种场景怎么配

### 2.1 常驻鼠标（TopDown 鼠标朝向 + WASD）

改**HUD 根 Layout widget**（本工程是 `Content/UI/HUD/W_HUDLayout`，由 Experience 的
`LAS_Standard_HUD` ActionSet 推入 `UI.Layer.Game`）：

```text
W_HUDLayout (ULyraActivatableWidget)
  InputConfig          = GameAndMenu
  GameMouseCaptureMode = NoCapture
  bIgnoreLookInput     = false
```

Lyra 原版是 `Game + CapturePermanently`（FPS 锁鼠标），TopDown 要改的就是这两项；
朝向和 WASD 输入由 `UTopDownPawnComponent` 的 Enhanced Input 绑定负责。

### 2.2 临时鼠标模式（建造模式）

不要在 Ability 里写 `PC->SetShowMouseCursor(true)`。正确做法是 push 一个自己声明输入配置的
widget，结束时 pop，栈会自动把配置回退到下层：

```cpp
// GA_Build_Mode::ActivateAbility
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_UI_LAYER_GAME, "UI.Layer.Game");

if (ULocalPlayer* LP = GetLocalPlayerFromActorInfo())
{
    BuildHUD = UCommonUIExtensions::PushContentToLayer_ForPlayer(LP, TAG_UI_LAYER_GAME, BuildHUDClass);
}

// GA_Build_Mode::EndAbility
if (BuildHUD)
{
    UCommonUIExtensions::PopContentFromLayer(BuildHUD);
    BuildHUD = nullptr;
}
```

```text
W_BuildHUD (ULyraActivatableWidget)
  InputConfig          = GameAndMenu
  GameMouseCaptureMode = NoCapture
  bIgnoreLookInput     = true      // 建造时不想让鼠标带动相机就打开
```

**栈式回退是这套设计的核心价值**：不需要保存/恢复上一次的鼠标状态，也不会因为中途弹出
设置菜单再关掉而丢失建造模式的配置。

### 2.3 全屏菜单（背包、设置）

`InputConfig = Menu`，CommonUI 自动 `NoCapture` 并接管手柄焦点导航。菜单关闭后，
配置回到下面的 HUD 层，鼠标模式无缝还原。

---

## 3. 本工程需要修的一处

`UTopDownPawnComponent::BindInput()` 不读写 `APlayerController::bShowMouseCursor`，而是绑定
PawnData 的 `InputConfig`，并 push 一个声明 `GameAndMenu + NoCapture` 的 CommonUI widget。
任何 `ULyraActivatableWidget` 激活或失活时，ActionRouter 都会按栈顶声明重新应用输入配置，
因此鼠标状态不会被组件保存的中间值覆盖。组件只保留鼠标平面朝向、WASD 移动与镜头输入，
而输入模式继续由 widget 栈统一管理。

---

## 4. 光标外观

- **系统光标**：`Project Settings → User Interface → Hardware Cursors`，把
  `EMouseCursor` 各状态映射到 `.png/.cur` 资产；运行时用
  `APlayerController::CurrentMouseCursor` 切换（例如建造合法/非法用不同光标）。
- **建造模式建议隐藏系统光标**（`CurrentMouseCursor = EMouseCursor::None`），
  改用世界里的幽灵物体 + 高亮格子表达位置 —— 3D 建造的反馈应该在世界里，不在 2D 光标上。
- 悬停 UI 按钮时 CommonUI 会自己切回默认箭头，不需要手动管理。

---

## 5. 手柄兼容（做之前先想清楚）

鼠标模式在手柄下必须有对应方案，否则整套建造在手柄上不可用：

```cpp
if (UCommonInputSubsystem* Input = UCommonInputSubsystem::Get(LocalPlayer))
{
    const bool bGamepad = Input->GetCurrentInputType() == ECommonInputType::Gamepad;
    // 手柄：射线从屏幕中心固定发出，用摇杆推动幽灵；键鼠：射线跟随光标
}
```

`UCommonInputSubsystem::OnInputMethodChangedNative` 可以订阅输入方式切换，
在两套定位方案之间实时切换。Lyra 的 `Menu` 模式已经处理了手柄的焦点导航，
真正要自己写的只有"建造定位"这一段。

---

## 6. 排错

| 现象 | 原因 |
| --- | --- |
| 光标偶尔消失 / 被莫名重置 | 有代码直接调 `SetShowMouseCursor`，与 ActionRouter 冲突（见第 3 节） |
| 鼠标可见但点不到 UI | `InputConfig = Game`，输入没进 Menu 通道；改 `GameAndMenu` |
| 能点 UI 但 WASD 失灵 | `InputConfig = Menu`，游戏输入被屏蔽；改 `GameAndMenu` |
| 光标可见但一动相机就跑掉 | `GameMouseCaptureMode` 不是 `NoCapture` |
| 关掉设置菜单后鼠标模式没了 | 依赖了手动保存/恢复，而不是 widget 栈；按第 2.2 节改成 push/pop |
| 建造 HUD 弹出后角色不动了 | `bIgnoreLookInput` 或 `Menu` 模式误用；建造应保持 `GameAndMenu` |
