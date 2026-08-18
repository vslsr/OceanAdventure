// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

namespace OceanCore
{
	/** Hard runtime limit shared by chunk generation, RPC validation, and network culling. */
	inline constexpr int32 MaxSupportedChunkRadius = 16;

	/** Extra chunk-width allowance used by the coarse network cull distance. */
	inline constexpr float ChunkCullDistanceMargin = 1.0f;
}
