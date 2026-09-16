# Build Failure Triage (UBT / UHT)

编译失败和运行期故障不是一类问题：这里的证据在 UBT 输出里，不在 Output Log 里，
而且**最常见的几种报错都会把人引向错误的方向**——它们看起来像代码写错了或者本地没拉全，
实际上前两种是 `Intermediate/` 陈旧、第三种是有人把一次改动拆开提交了、
第四种是 unity build 把一个 `using namespace` 漏给了同批次的其他文件。
四种的处理互不相同，所以先对签名、再动手。

## 四个会误导人的签名

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

### 3. `#include` 的**头文件**报 C1083——先分清是缺文件还是缓存

```
SomeFile.cpp(3,1): Error C1083: 无法打开包括文件: "Terrain/SomeFile.h"
```

和第 2 种长得像，**但处理完全相反**，而且最容易把人引向「我是不是没拉全」：

| | 第 2 种：陈旧缓存 | 第 3 种：不完整落地 |
| --- | --- | --- |
| 报错指向 | `.cpp`（unity blob 里的一行） | `.h`（源文件自己的 `#include`） |
| 那个文件在仓库里 | **在** | **不在** |
| 处理 | 删 `Intermediate/` | 把缺的文件合过来，删缓存没用 |

**30 秒判别**——问的是「远端有没有这个文件」，不是「我本地是不是旧了」：

```bash
git ls-files "*/SomeFile.h"                 # 当前检出里有吗
git log --oneline --all -- "*/SomeFile.h"   # 哪个提交带来它，在哪条分支上
```

两条都空 → 那是引擎头，检查模块的 `Build.cs` 依赖漏了哪个。
第一条空、第二条有 → **文件在别的分支上，当前分支不完整**。这时 `git pull` 再多次也没用，
远端那个分支上本来就没有它。

**为什么会出现不完整的分支**：有人把一次改动的一部分单独提交上去了——最常见的是
GitHub 网页版的「Create file」，它一次只能提交一个文件且直接落在 main。
规则见 `AGENTS.md` 的「源码变更必须整套落地」。

### 4. C4459「声明隐藏了全局声明」，而且报在你没碰过的文件上

```
OceanChunkActor.cpp(24,48): Error C4459: "ChunkSize"的声明隐藏了全局声明
    float CalculateChunkCullDistanceSquared(float ChunkSize)
note: 参见"OceanTerrain::ChunkSize"的声明
```

你这次只新增了一个文件，报错却出现在另外三个**完全没动过**的 `.cpp` 里。

**原因是 unity build。** UBT 把同一模块的多个 `.cpp` 拼进一个
`Module.<模块名>.cpp` 一起编。于是**文件作用域的 `using namespace X;` 会泄漏到整个
拼接单元**——后面那些文件里，凡是叫 `ChunkSize`、`CellSize`、`NoiseScale` 的局部变量
或参数，都突然「隐藏」了 `X::` 里的同名常量。UE 把 C4459 当错误，编译直接失败。

写在匿名 namespace 里**也照样泄漏**：

```cpp
namespace
{
    using namespace OceanTerrain;   // ← 仍然漏到整个 blob
}
```

**处理**：把 `using namespace` 从文件作用域拿掉。

- 名字少就全限定：`OceanTerrain::FTerrainEditor`；
- 名字多、集中在一个函数里，就把 `using namespace` **收进那个函数体**——
  函数内的 using 不会越过函数边界。

**判据**：报错的文件你这次没改过，而 `note:` 指向的是你**刚加的头或刚加的文件**引入的名字。
这时不要去改被报错的那个文件（它没错），去找本次新增文件里的 `using`。

> 顺带：这也是为什么公共头里不要写 `using namespace`。上面第 3 条那个设计教训
> （public 头加 include 是高成本改动）和这条是同一类——**在共享的编译单元里引入名字，
> 代价由别的文件付**。

## 处理

**第 1、2 种同因**（陈旧 `Intermediate/`），同一个动作解决；**第 3、4 种都不是**——
第 3 种缺的是文件本身，第 4 种是代码问题，对它们删缓存只会浪费一次全量重编。
先按第 3 种的两条 `git` 命令、第 4 种的判据排除掉，再做下面这步——**删掉 `Intermediate/`，包括插件自己的那份**：

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

- 2026-09-16：新增 `OceanTerrainConsole.cpp`，在匿名 namespace 里写了
  `using namespace OceanTerrain;`。编译报 4 处 C4459，全部落在
  `OceanChunkActor.cpp` / `OceanChunkInvokerComponent.cpp` / `OceanGenerationSettings.cpp`
  ——这次一行都没改过的三个文件。同一模块里 `OceanTerrainChunkComponent.cpp` 还有一处
  文件作用域的 `using namespace UE::Geometry;`，当时还没炸，但是同一颗地雷，一并收进了函数体。

- 2026-09-16：`76b5c25` 用 GitHub 网页版把 `OceanTerrainMeshBuilder.cpp` 单独提交到 main，
  它依赖的 22 个地形源文件还在功能分支上。main 编译报
  `C1083: 无法打开包括文件 "Terrain/OceanTerrainMeshBuilder.h"`。
  排查先走了「是不是没拉全」——验证了本地 HEAD、`origin/main` 与实时远端三者一致、
  工作区干净、无子模块，**全部正常**，因为问题不在拉取侧。
  把完整分支合入 main 后解决。上面第 3 个签名就是为这次写的。

- 2026-09-16：为「让 `ChunkSize` 从 `ChunkGrid` 派生」，往 `OceanChunkActor.h`、
  `OceanWorldManagerComponent.h`、`OceanGenerationSettings.h` 三个 public 头各加了一行
  `#include "Terrain/OceanTerrainTypes.h"`。切分支后首次编译，三个下游模块
  （`OceanAdventureRuntime`、`RaftRuntime`、`OceanCoreEditor`）同时报
  `OceanGenerationSettings.h(18,28) C2143`，`OceanCoreRuntime` 则报一个确实存在的
  `OceanTerrainOutline.cpp` 找不到。两者同因（陈旧 `Intermediate/`）。
  改成字面量 + `.cpp` 里的 `static_assert`，并记下上面这条设计教训。
