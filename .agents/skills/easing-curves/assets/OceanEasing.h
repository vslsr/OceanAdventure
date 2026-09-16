// Copyright OceanAdventure. All Rights Reserved.
//
// 缓动曲线库：easings.net 上那 31 条标准曲线（linear + 10 族 × In/Out/InOut）的 UE 实现。
//
// 用法：把本文件复制进目标模块的 Public/ 下（放哪个模块看谁用——只有一个 GameFeature 用就放进
// 那个 GameFeature 的模块，跨插件共用才下沉到通用插件），然后 #include "OceanEasing.h"。
// 本文件**故意不带任何 UHT 宏**（无 UCLASS/UENUM/USTRUCT，不需要 .generated.h），
// 所以扔进任何模块都能编。要暴露给 Blueprint 时，在那个模块里另写一个
// UBlueprintFunctionLibrary 包一层，别改本文件。
//
// 约定：所有函数吃归一化进度 X ∈ [0,1]，返回插值系数。函数**不夹取**输入也不夹取输出：
//   - X 必须由调用方按 Elapsed / Duration 算好（别把 DeltaTime 直接喂进来）；
//   - Back / Elastic / Bounce 的返回值会冲出 [0,1]（过冲/回弹），这是它们的手感来源，
//     不要顺手 Clamp，否则曲线被削平。夹取会出事的地方（半径、缩放、颜色分量）见
//     ../references/unreal-mapping.md「三条会咬人的坑」。
//
// 公式取自 easings.net（Robert Penner 的经典定义），逐条对照过
// ../references/easings.net/easings.yml；那份参考副本是 GPL-3.0，只作查阅，
// 本文件是按公式独立实现的，可直接进工程。

#pragma once

#include "Math/UnrealMathUtility.h"

namespace OceanEasing
{
	// ---- 常数（沿用 easings.net 的命名，便于和公式逐字对照）--------------------
	inline constexpr float C1 = 1.70158f;          // Back：过冲量
	inline constexpr float C2 = C1 * 1.525f;       // InOutBack
	inline constexpr float C3 = C1 + 1.0f;         // In/OutBack
	inline constexpr float C4 = (2.0f * UE_PI) / 3.0f;    // In/OutElastic：振荡角频率
	inline constexpr float C5 = (2.0f * UE_PI) / 4.5f;    // InOutElastic

	// ---- Linear ---------------------------------------------------------------
	inline float Linear(float X) { return X; }

	// ---- Sine：最轻的一档，几乎察觉不到“被加速过”-------------------------------
	inline float InSine(float X) { return 1.0f - FMath::Cos((X * UE_PI) / 2.0f); }
	inline float OutSine(float X) { return FMath::Sin((X * UE_PI) / 2.0f); }
	inline float InOutSine(float X) { return -(FMath::Cos(UE_PI * X) - 1.0f) / 2.0f; }

	// ---- Quad / Cubic / Quart / Quint：同一条幂曲线，指数 2/3/4/5 逐档更重 -----
	inline float InQuad(float X) { return X * X; }
	inline float OutQuad(float X) { return 1.0f - (1.0f - X) * (1.0f - X); }
	inline float InOutQuad(float X)
	{
		return X < 0.5f ? 2.0f * X * X : 1.0f - FMath::Pow(-2.0f * X + 2.0f, 2.0f) / 2.0f;
	}

	inline float InCubic(float X) { return X * X * X; }
	inline float OutCubic(float X) { return 1.0f - FMath::Pow(1.0f - X, 3.0f); }
	inline float InOutCubic(float X)
	{
		return X < 0.5f ? 4.0f * X * X * X : 1.0f - FMath::Pow(-2.0f * X + 2.0f, 3.0f) / 2.0f;
	}

	inline float InQuart(float X) { return X * X * X * X; }
	inline float OutQuart(float X) { return 1.0f - FMath::Pow(1.0f - X, 4.0f); }
	inline float InOutQuart(float X)
	{
		return X < 0.5f ? 8.0f * X * X * X * X : 1.0f - FMath::Pow(-2.0f * X + 2.0f, 4.0f) / 2.0f;
	}

	inline float InQuint(float X) { return X * X * X * X * X; }
	inline float OutQuint(float X) { return 1.0f - FMath::Pow(1.0f - X, 5.0f); }
	inline float InOutQuint(float X)
	{
		return X < 0.5f ? 16.0f * X * X * X * X * X : 1.0f - FMath::Pow(-2.0f * X + 2.0f, 5.0f) / 2.0f;
	}

	// ---- Expo：起步几乎不动 / 收尾几乎瞬停，最“狠”的一档 -----------------------
	// 端点特判不能省：Pow(2, -10 * 0) == 1，去掉它 X=0 处会直接跳到终点。
	inline float InExpo(float X)
	{
		return X == 0.0f ? 0.0f : FMath::Pow(2.0f, 10.0f * X - 10.0f);
	}
	inline float OutExpo(float X)
	{
		return X == 1.0f ? 1.0f : 1.0f - FMath::Pow(2.0f, -10.0f * X);
	}
	inline float InOutExpo(float X)
	{
		if (X == 0.0f) { return 0.0f; }
		if (X == 1.0f) { return 1.0f; }
		return X < 0.5f
			? FMath::Pow(2.0f, 20.0f * X - 10.0f) / 2.0f
			: (2.0f - FMath::Pow(2.0f, -20.0f * X + 10.0f)) / 2.0f;
	}

	// ---- Circ：圆弧，收尾比 Quart 更“刹得住”------------------------------------
	inline float InCirc(float X) { return 1.0f - FMath::Sqrt(1.0f - X * X); }
	inline float OutCirc(float X) { return FMath::Sqrt(1.0f - (X - 1.0f) * (X - 1.0f)); }
	inline float InOutCirc(float X)
	{
		return X < 0.5f
			? (1.0f - FMath::Sqrt(1.0f - FMath::Pow(2.0f * X, 2.0f))) / 2.0f
			: (FMath::Sqrt(1.0f - FMath::Pow(-2.0f * X + 2.0f, 2.0f)) + 1.0f) / 2.0f;
	}

	// ---- Back：先反向蓄一下再冲出去；返回值会 < 0 或 > 1 -----------------------
	inline float InBack(float X) { return C3 * X * X * X - C1 * X * X; }
	inline float OutBack(float X)
	{
		return 1.0f + C3 * FMath::Pow(X - 1.0f, 3.0f) + C1 * FMath::Pow(X - 1.0f, 2.0f);
	}
	inline float InOutBack(float X)
	{
		return X < 0.5f
			? (FMath::Pow(2.0f * X, 2.0f) * ((C2 + 1.0f) * 2.0f * X - C2)) / 2.0f
			: (FMath::Pow(2.0f * X - 2.0f, 2.0f) * ((C2 + 1.0f) * (X * 2.0f - 2.0f) + C2) + 2.0f) / 2.0f;
	}

	// ---- Elastic：衰减振荡，来回穿越终点若干次 --------------------------------
	inline float InElastic(float X)
	{
		if (X == 0.0f) { return 0.0f; }
		if (X == 1.0f) { return 1.0f; }
		return -FMath::Pow(2.0f, 10.0f * X - 10.0f) * FMath::Sin((X * 10.0f - 10.75f) * C4);
	}
	inline float OutElastic(float X)
	{
		if (X == 0.0f) { return 0.0f; }
		if (X == 1.0f) { return 1.0f; }
		return FMath::Pow(2.0f, -10.0f * X) * FMath::Sin((X * 10.0f - 0.75f) * C4) + 1.0f;
	}
	inline float InOutElastic(float X)
	{
		if (X == 0.0f) { return 0.0f; }
		if (X == 1.0f) { return 1.0f; }
		return X < 0.5f
			? -(FMath::Pow(2.0f, 20.0f * X - 10.0f) * FMath::Sin((20.0f * X - 11.125f) * C5)) / 2.0f
			: (FMath::Pow(2.0f, -20.0f * X + 10.0f) * FMath::Sin((20.0f * X - 11.125f) * C5)) / 2.0f + 1.0f;
	}

	// ---- Bounce：落地弹跳，只在终点侧回弹，不越过终点 --------------------------
	// 注意分段里对 X 的减法作用在**形参副本**上，照抄公式时别写成改了调用方的值。
	inline float OutBounce(float X)
	{
		constexpr float N1 = 7.5625f;
		constexpr float D1 = 2.75f;

		if (X < 1.0f / D1)
		{
			return N1 * X * X;
		}
		if (X < 2.0f / D1)
		{
			X -= 1.5f / D1;
			return N1 * X * X + 0.75f;
		}
		if (X < 2.5f / D1)
		{
			X -= 2.25f / D1;
			return N1 * X * X + 0.9375f;
		}
		X -= 2.625f / D1;
		return N1 * X * X + 0.984375f;
	}
	inline float InBounce(float X) { return 1.0f - OutBounce(1.0f - X); }
	inline float InOutBounce(float X)
	{
		return X < 0.5f
			? (1.0f - OutBounce(1.0f - 2.0f * X)) / 2.0f
			: (1.0f + OutBounce(2.0f * X - 1.0f)) / 2.0f;
	}

	// ---- 便利封装 -------------------------------------------------------------
	// 典型用法：Alpha = OceanEasing::Normalize(Elapsed, Duration); Value = FMath::Lerp(A, B, OceanEasing::OutBack(Alpha));
	// 只有进度需要夹取（时间可能超过 Duration），曲线输出不夹。
	inline float Normalize(float Elapsed, float Duration)
	{
		return Duration > UE_SMALL_NUMBER ? FMath::Clamp(Elapsed / Duration, 0.0f, 1.0f) : 1.0f;
	}
}
