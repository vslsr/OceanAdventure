# C++ 源码调查指南

## 何时调查 C++ 源码

Python API 由 C++ 通过反射生成。C++ 源码是实际情况。

在以下情况下使用此方法:

- Python API 存在但行为异常
- 文档不清楚
- 需要理解参数约束
- 寻找替代方法
- 调查构造函数可用性

## Engine Source Locations

根据操作系统选择合适的路径:

- **macOS**: `/Users/Shared/Epic Games/UE_5.X/Engine/Source`
- **Windows**: `C:\Program Files\Epic Games\UE_5.X\Engine\Source`
- **Linux**: `~/UnrealEngine/Engine/Source` (或自定义安装位置)

## 快速调查工作流

### 示例: 调查 GameplayTag 创建

```bash
# 1. 导航到引擎源码
cd "/Users/Shared/Epic Games/UE_5.5/Engine/Source"

# 2. 查找类头文件
grep -r "class.*GameplayTag" --include="*.h"

# 3. 阅读头文件理解:
#    - 构造函数签名(公有 vs 私有)
#    - 静态工厂方法
#    - 参数类型和默认值

# 4. 查找实现细节
grep -r "RequestGameplayTag" --include="*.cpp"
```

## 理解 UFUNCTION 宏

在 C++ 头文件中查找这些宏 - 它们控制 Python 暴露:

```cpp
// 暴露给 Python 和 Blueprints
UFUNCTION(BlueprintCallable)
void MyMethod();

// 暴露给 Blueprints(通常也暴露给 Python)
UFUNCTION(BlueprintPure)
int32 GetValue() const;

// 未暴露 - 不会在 Python API 中
void PrivateMethod();
```

## Custom C++ Utility Class Solution

如果 C++ 有简单的 API 但 Python 缺乏适当的绑定:

### 何时使用

- C++ 为任务提供了简单的函数
- Python API 缺失或尴尬
- 示例:GameplayTag 创建可能有简单的 C++ 辅助程序但 Python 访问受限

### 解决方案: 创建自定义 C++ 工具类

```cpp
// MyPythonUtils.h
UCLASS()
class UMyPythonUtils : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

    UFUNCTION(BlueprintCallable, Category="Python Utilities")
    static FGameplayTag CreateGameplayTag(FName TagName);
};

// MyPythonUtils.cpp
FGameplayTag UMyPythonUtils::CreateGameplayTag(FName TagName)
{
    // 使用有效的 C++ API
    return FGameplayTag::RequestGameplayTag(TagName);
}
```

编译后,在 Python 中可用:

```python
import unreal
tag = unreal.MyPythonUtils.create_gameplay_tag(unreal.Name("Ability.Melee"))
```

**这在 Python API 不足时弥补了差距。**

## 常见调查模式

### Pattern 1: 查找工厂方法

问题: 无法直接构造对象

```bash
grep -r "Create.*\|Request.*\|Make.*" --include="*.h" | grep "static"
```

查找返回相同类型的静态方法。

### Pattern 2: 查找属性访问器

问题: 属性无法从 Python 访问

```bash
grep -r "Get.*\|Set.*" --include="*.h" | grep "UPROPERTY\|UFUNCTION"
```

查找带 `UPROPERTY()` 或 `UFUNCTION()` 宏的属性访问器。

### Pattern 3: 理解构造函数可见性

问题: 无法实例化类

```bash
# 查找类定义
grep -r "class.*MyClass" --include="*.h" -A 10

# 查看构造函数部分:
# - public: (可构造)
# - private: (需要工厂方法)
# - protected: (需要子类)
```

### Pattern 4: 查找回调和委托

问题: 需要设置事件处理程序

```bash
grep -r "FSimpleDelegate\|FSimpleEventBase\|Declare.*Delegate" --include="*.h"
```

查找委托声明和绑定方法。

## 实际示例

### 示例 1: 查找 GameplayTag 创建方法

**问题:** `unreal.GameplayTag()` 构造函数失败

**调查步骤:**

```bash
# 查找 GameplayTag 类
grep -r "class FGameplayTag" --include="*.h"

# 查找相关方法
grep -r "FGameplayTag\|GameplayTag" /path/to/GameplayTags.h | grep -i "static\|create\|request"
```

**发现:** 存在 `FGameplayTag::RequestGameplayTag()` 静态方法

**解决方案:** 创建 C++ 工具类包装该方法

### 示例 2: 查找资产属性

**问题:** `asset.some_property` 在 Python 中不可访问

**调查步骤:**

```bash
# 在相关头文件中搜索属性
grep -r "UPROPERTY\|UFUNCTION" MyAssetClass.h | grep -i "some_property"

# 检查是否标记为暴露给 Blueprint/Python
```

**发现:** 属性存在但未标记为 `BlueprintReadOnly` 或 `BlueprintReadWrite`

**解决方案:** 创建访问器函数或编辑类的 C++ 定义

## 最佳实践

1. **首先尝试 Python API** - 不是所有问题都需要 C++ 调查
2. **使用 search-api** - 在调查前验证 API 是否存在
3. **查看文档** - 许多类在头文件中有详细注释
4. **记录发现** - 在脚本中记录为什么使用特定方法
5. **考虑 C++ 工具类** - 如果多个脚本需要相同功能

## 相关资源

- [Best Practices](best-practices.md) - UE5 特有的模式和最佳实践
