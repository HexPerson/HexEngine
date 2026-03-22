#pragma once

#include "../HexEngine.hpp"

namespace HexEngine::SceneFramingUtils
{
	bool HEX_API ComputeSceneBounds(
		Scene* scene,
		math::Vector3& outBoundsMin,
		math::Vector3& outBoundsMax,
		bool ignoreDoNotSaveEntities = true);

	bool HEX_API FrameCameraToSceneBounds(
		Scene* scene,
		Camera* camera,
		bool ignoreDoNotSaveEntities = true);
}

