# 项目规范

## Blender 脚本

- 所有 Blender Python 脚本统一放置在项目根目录下的 `blender/script/python/` 目录中。
- 除非任务明确要求，否则只生成脚本，不启动、连接或操作 Blender。

## Lyra 参考项目

- `../LyraStarterGame` 是完整的 Lyra 项目，可作为功能、内容资产和实现方式的参考。

## GameFeature 内容归属

- 严禁将属于某个 GameFeature 概念的任何内容放置在该 GameFeature 目录之外。
- GameFeature 的 C++ 代码、Blueprint、内容资产、DataAsset、材质、地图、配置、Python 脚本及其他专属资源，必须放置在 `Plugins/GameFeatures/<GameFeatureName>/` 对应目录下。
- 不得将 GameFeature 专属内容放入项目 `/Game` 内容目录、项目根 `Source/` 或其他插件目录。
- 只有经确认与任何单一 GameFeature 无关、可被多个系统共享的通用能力，才可放入项目或通用插件目录。
