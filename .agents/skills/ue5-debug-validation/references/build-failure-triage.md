# Build Failure Triage (UBT / UHT)

编译失败和运行期故障不是一类问题：这里的证据在 UBT 输出里，不在 Output Log 里，
而且**最常见的两种报错都会把人引向错误的方向**——它们看起来像代码写错了，其实是
`Intermediate/` 陈旧。

## 两个会误导人的签名

### 1. `UCLASS(...)` 报 C4430 / 下一行报 C2143

```
SomeHeader.h(17,1): Error C4430: 缺少类型说明符 - 假定为 int
UCLASS(BlueprintType)
SomeHeader.h(18,28): Error C2143: 语法错误: 缺少";"(在"<class-head>"的前面)
class SOMEMODULE_API USomeClass : public UObject
```

**不是 include 顺序写错了。** `UCLASS()` 在 `ObjectMacros.h` 里展开成
`FID_<文件路径>_<行号>_PROLOG`——**按行号命名的宏**，由该文件的 `.generated.h` 定义。
`.generated.h` 一旦过期，行号对不上，那个宏就没有定义，于是
`UCLASS(BlueprintType)` 被当成 `int UCLASS(BlueprintType);` 一个函数声明，下一行自然语法错误。

**触发条件**：任何让 `UCLASS`/`USTRUCT`/`UENUM` 行号位移的改动——**在 include 块里加或删一行**
就够了——再叠加上 UHT 没有重跑。

**不要**去调 include 顺序、给 `.generated.h` 换位置、或者给类加 `class` 前置声明。那些都改不动它。

### 2. 明明存在的 `.cpp` 报 C1083 找不到

```
Module.SomeModule.cpp(18,1): Error C1083: 无法打开包括文件: ".../Private/Foo/Bar.cpp"
```

文件就在磁盘上、也在 git 里。报错的是 **unity blob**（`Module.*.cpp`），它是 UBT 生成的，
内容来自 UBT **缓存的源文件清单**。清单陈旧时就会指向一个当前状态下不该出现或还没落盘的路径。

**常见触发**：切分支（尤其是分支间新增/删除了源文件）、第一次把一批新文件拉进来、
中途打断过一次编译。

## 处理

两种签名同因，同一个动作解决——**删掉 `Intermediate/`，包括插件自己的那份**：

```powershell
Remove-Item -Recurse -Force .\Intermediate, .\Plugins\<PluginName>\Intermediate -ErrorAction SilentlyContinue
& "$Engine\Engine\Build\BatchFiles\Build.bat" <Target> Win64 Development "-Project=$PWD\<Project>.uproject" -WaitMutex -NoHotReloadFromIDE -NoUBA -MaxParallelActions=1
```

判据：重编时 UHT 会重新生成，`.generated.h` 的行号与当前头文件对齐，两类报错一起消失。
如果删完仍报同样的错，才回头怀疑代码——那时才值得看 include 顺序和 UHT 对该头的解析。

## 反过来的设计教训：public 头加 include 是高成本改动

给一个被广泛包含的 public 头加一行 `#include`，代价不止是「多一个依赖」：

1. 它**移动了该文件后面所有反射宏的行号**，于是每个包含它的模块都要重新 UHT + 重编；
2. 陈旧 `Intermediate/` 下，它就是上面第 1 种报错的直接触发器；
3. 它还可能**把依赖方向弄反**——比如底层的 `World/` 头去包含上层 `Terrain/` 的类型。

当目的只是「让两个常量保持相等」这类编译期保证时，优先这个写法：

```cpp
// SomeHeader.h —— 字面量，不引入依赖
float ChunkSize = 6400.0f;

// SomeHeader.cpp —— 在实现里钉住，漂移就编译不过
#include "Terrain/OceanTerrainTypes.h"
static_assert(
    OceanTerrain::ChunkGrid * OceanTerrain::CellSize == 6400.0f,
    "ChunkSize default must stay equal to ChunkGrid * CellSize.");
```

保证一样强（编译期），但依赖只存在于 `.cpp`，头文件的行号和依赖图都不动。

## 已发生记录

- 2026-09-16：为「让 `ChunkSize` 从 `ChunkGrid` 派生」，往 `OceanChunkActor.h`、
  `OceanWorldManagerComponent.h`、`OceanGenerationSettings.h` 三个 public 头各加了一行
  `#include "Terrain/OceanTerrainTypes.h"`。切分支后首次编译，三个下游模块
  （`OceanAdventureRuntime`、`RaftRuntime`、`OceanCoreEditor`）同时报
  `OceanGenerationSettings.h(18,28) C2143`，`OceanCoreRuntime` 则报一个确实存在的
  `OceanTerrainOutline.cpp` 找不到。两者同因（陈旧 `Intermediate/`）。
  改成字面量 + `.cpp` 里的 `static_assert`，并记下上面这条设计教训。
