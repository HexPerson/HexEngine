#pragma once

#include "../Entity/Component/StaticMeshComponent.hpp"

namespace HexEngine
{
	class Camera;
	class Scene;

	class HEX_API ISceneCustomRenderer
	{
	public:
		virtual ~ISceneCustomRenderer() = default;
		virtual void RenderCustom(Scene* scene, Camera* camera, MeshRenderFlags renderFlags) = 0;
	};
}
