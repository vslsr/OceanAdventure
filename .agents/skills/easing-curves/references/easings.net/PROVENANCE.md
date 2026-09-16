# 参考源码出处（easings.net）

本目录是**上游参考源码的裁剪副本**，只作查阅用，不参与编译、不进打包。

| 项 | 值 |
|---|---|
| 上游仓库 | https://github.com/ai/easings.net （站点 https://easings.net/） |
| 拉取命令 | `git clone --depth 1 https://github.com/ai/easings.net.git` |
| 拉取时的提交 | `d2563f0d32a511b5556774b838ec35c3a841b15d`（2026-04-07） |
| 上游许可证 | GPL-3.0，全文见同目录 `LICENSE` |

## 保留了什么

| 文件 | 上游路径 | 内容 |
|---|---|---|
| `easings.yml` | `src/easings.yml` | 31 条曲线的**权威数据源**：`name` + `css`（cubic-bezier 值，Elastic/Bounce 为 `no`）+ `maths`（站点上展示的 JS 公式）。站点页面就是由它生成的。 |
| `easingsFunctions.ts` | `src/easings/easingsFunctions.ts` | 同一批公式的可运行 TS 实现（`bounceOut` 抽成了共用函数）。 |
| `LICENSE` | `LICENSE` | 上游许可证原文。 |

删掉的是站点渲染用的 pug/css/ts（`easings.ts`、`keyframes.ts`、`keyframes.css` 等），与曲线本身无关。

## 许可证边界（照做，别绕）

- **这两个源码文件是 GPL-3.0 的，不要把它们的代码形态复制进 `Source/` 或 `Plugins/`**，
  也不要在引擎里 `#include` 本目录任何东西。它们在这里只回答一个问题：“这条曲线的公式到底是什么”。
- 可以放心落地的是**公式本身**——这批缓动是 Robert Penner 公开的经典数学定义，
  数学公式不受版权保护；`../../assets/OceanEasing.h` 就是按公式独立写的 UE 版实现，
  不是对上游文件的翻译或逐行移植，可以直接进工程。
- 升级参考副本时重跑上面的 clone，只覆盖 `easings.yml` / `easingsFunctions.ts` / `LICENSE`，
  并更新本文件里的提交号。
