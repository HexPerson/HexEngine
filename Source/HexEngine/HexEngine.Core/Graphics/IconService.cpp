

#include "IconService.hpp"
#include "../FileSystem/PrefabLoader.hpp"
#include "../Scene/PVS.hpp"
#include "../Scene/SceneFramingUtils.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <unordered_set>

namespace
{
	bool IsImageExtension(const std::string& extLower)
	{
		return extLower == ".png" ||
			extLower == ".jpg" ||
			extLower == ".jpeg" ||
			extLower == ".dds" ||
			extLower == ".tga" ||
			extLower == ".bmp";
	}

	void CollectHierarchyEntities(HexEngine::Entity* root, std::vector<HexEngine::Entity*>& outEntities)
	{
		if (root == nullptr || root->IsPendingDeletion())
			return;

		outEntities.push_back(root);

		for (auto* child : root->GetChildren())
		{
			CollectHierarchyEntities(child, outEntities);
		}
	}

	std::vector<HexEngine::Entity*> CollectUniquePrefabRoots(const std::vector<HexEngine::Entity*>& spawnedEntities)
	{
		std::vector<HexEngine::Entity*> roots;
		std::unordered_set<HexEngine::Entity*> visited;
		roots.reserve(spawnedEntities.size());

		for (auto* entity : spawnedEntities)
		{
			if (entity == nullptr || entity->IsPendingDeletion())
				continue;

			auto* root = entity;
			while (root->GetParent() != nullptr)
			{
				root = root->GetParent();
			}

			if (root != nullptr && visited.insert(root).second)
			{
				roots.push_back(root);
			}
		}

		return roots;
	}

	bool ComputeBoundsFromPreviewRoots(
		const std::vector<HexEngine::Entity*>& rootEntities,
		math::Vector3& outBoundsMin,
		math::Vector3& outBoundsMax)
	{
		math::Vector3 meshBoundsMin(std::numeric_limits<float>::max());
		math::Vector3 meshBoundsMax(std::numeric_limits<float>::lowest());
		bool hasMeshBounds = false;

		math::Vector3 fallbackBoundsMin(std::numeric_limits<float>::max());
		math::Vector3 fallbackBoundsMax(std::numeric_limits<float>::lowest());
		bool hasFallbackBounds = false;

		std::vector<HexEngine::Entity*> entities;
		entities.reserve(rootEntities.size() * 4);
		for (auto* root : rootEntities)
		{
			CollectHierarchyEntities(root, entities);
		}

		for (auto* entity : entities)
		{
			if (entity == nullptr || entity->IsPendingDeletion())
				continue;

			if (auto* meshComponent = entity->GetComponent<HexEngine::StaticMeshComponent>(); meshComponent != nullptr && meshComponent->GetMesh() != nullptr)
			{
				if (entity->HasFlag(HexEngine::EntityFlags::DoNotRender))
					continue;

				const auto& worldAabb = entity->GetWorldAABB();
				const math::Vector3 center(worldAabb.Center);
				const math::Vector3 extents(worldAabb.Extents);
				const math::Vector3 aabbMin = center - extents;
				const math::Vector3 aabbMax = center + extents;

				meshBoundsMin.x = std::min(meshBoundsMin.x, aabbMin.x);
				meshBoundsMin.y = std::min(meshBoundsMin.y, aabbMin.y);
				meshBoundsMin.z = std::min(meshBoundsMin.z, aabbMin.z);

				meshBoundsMax.x = std::max(meshBoundsMax.x, aabbMax.x);
				meshBoundsMax.y = std::max(meshBoundsMax.y, aabbMax.y);
				meshBoundsMax.z = std::max(meshBoundsMax.z, aabbMax.z);

				hasMeshBounds = true;
				continue;
			}

			const auto worldPosition = entity->GetWorldTM().Translation();
			const math::Vector3 minPos = worldPosition - math::Vector3(0.25f);
			const math::Vector3 maxPos = worldPosition + math::Vector3(0.25f);

			fallbackBoundsMin.x = std::min(fallbackBoundsMin.x, minPos.x);
			fallbackBoundsMin.y = std::min(fallbackBoundsMin.y, minPos.y);
			fallbackBoundsMin.z = std::min(fallbackBoundsMin.z, minPos.z);

			fallbackBoundsMax.x = std::max(fallbackBoundsMax.x, maxPos.x);
			fallbackBoundsMax.y = std::max(fallbackBoundsMax.y, maxPos.y);
			fallbackBoundsMax.z = std::max(fallbackBoundsMax.z, maxPos.z);

			hasFallbackBounds = true;
		}

		if (hasMeshBounds)
		{
			outBoundsMin = meshBoundsMin;
			outBoundsMax = meshBoundsMax;
			return true;
		}

		if (hasFallbackBounds)
		{
			outBoundsMin = fallbackBoundsMin;
			outBoundsMax = fallbackBoundsMax;
			return true;
		}

		return false;
	}

	bool FrameCameraToPreviewRoots(
		HexEngine::Camera* camera,
		const std::vector<HexEngine::Entity*>& rootEntities)
	{
		if (camera == nullptr || camera->GetEntity() == nullptr)
			return false;

		math::Vector3 boundsMin;
		math::Vector3 boundsMax;
		if (!ComputeBoundsFromPreviewRoots(rootEntities, boundsMin, boundsMax))
			return false;

		const math::Vector3 center = (boundsMin + boundsMax) * 0.5f;
		const math::Vector3 extents = (boundsMax - boundsMin) * 0.5f;
		const float radius = std::max(0.5f, extents.Length());

		const float aspectRatio = std::max(0.01f, camera->GetAspectRatio());
		const float verticalFov = ToRadian(std::clamp(camera->GetFov(), 20.0f, 120.0f));
		const float horizontalFov = 2.0f * std::atan(std::tan(verticalFov * 0.5f) * aspectRatio);
		const float fittingHalfFov = std::max(0.1f, std::min(verticalFov, horizontalFov) * 0.5f);

		float distance = (radius / std::tan(fittingHalfFov)) * 1.15f;
		distance = std::max(distance, camera->GetNearZ() + radius + 0.5f);
		distance = std::min(distance, camera->GetFarZ() * 0.75f);

		math::Vector3 viewDirection(-1.0f, -0.65f, -1.0f);
		viewDirection.Normalize();

		const math::Vector3 cameraPosition = center - (viewDirection * distance);
		camera->GetEntity()->SetPosition(cameraPosition);

		math::Vector3 lookDirection = center - cameraPosition;
		if (lookDirection.Length() < 0.01f)
			lookDirection = math::Vector3::Forward;
		else
			lookDirection.Normalize();

		const float yaw = ToDegree(std::atan2(-lookDirection.x, -lookDirection.z));
		const float pitch = ToDegree(std::asin(std::clamp(lookDirection.y, -1.0f, 1.0f)));

		camera->SetYaw(yaw);
		camera->SetPitch(pitch);
		camera->SetRoll(0.0f);
		camera->Update(0.0f);
		return true;
	}

	void CollectSceneEntities(
		HexEngine::Scene* scene,
		std::vector<HexEngine::Entity*>& outEntities,
		HexEngine::Entity* excludeA = nullptr,
		HexEngine::Entity* excludeB = nullptr)
	{
		outEntities.clear();

		if (scene == nullptr)
			return;

		std::unordered_set<HexEngine::Entity*> unique;
		const auto& buckets = scene->GetEntities();
		for (const auto& bucket : buckets)
		{
			for (auto* entity : bucket.second)
			{
				if (entity == nullptr)
					continue;
				if (entity->IsPendingDeletion())
					continue;

				if (entity == excludeA || entity == excludeB)
					continue;

				if (unique.insert(entity).second)
				{
					outEntities.push_back(entity);
				}
			}
		}
	}

	void SeedPVSForPreview(HexEngine::Scene* scene, HexEngine::Camera* camera, const std::vector<HexEngine::Entity*>& rootEntities)
	{
		if (scene == nullptr || camera == nullptr)
			return;

		auto* cameraPvs = camera->GetPVS();
		if (cameraPvs != nullptr)
		{
			cameraPvs->ClearPVS();
		}

		if (auto* sun = scene->GetSunLight(); sun != nullptr)
		{
			for (int32_t i = 0; i < sun->GetMaxSupportedShadowCascades(); ++i)
			{
				sun->GetPVS(i)->ClearPVS();
			}
		}

		std::vector<HexEngine::Entity*> allPreviewEntities;
		allPreviewEntities.reserve(rootEntities.size() * 4);
		for (auto* root : rootEntities)
		{
			CollectHierarchyEntities(root, allPreviewEntities);
		}

		if (allPreviewEntities.empty() || cameraPvs == nullptr)
			return;

		for (auto* entity : allPreviewEntities)
		{
			if (entity == nullptr)
				continue;

			cameraPvs->AddEntity(entity);
		}

		if (auto* sun = scene->GetSunLight(); sun != nullptr)
		{
			for (int32_t i = 0; i < sun->GetMaxSupportedShadowCascades(); ++i)
			{
				auto* sunPvs = sun->GetPVS(i);
				for (auto* entity : allPreviewEntities)
				{
					if (entity == nullptr)
						continue;

					sunPvs->AddEntity(entity);
				}
			}
		}

		HexEngine::PVSParams params;
		params.lodPartition = camera->GetFarZ();
		params.shape.frustum.sm = camera->GetFrustum();
		params.shapeType = HexEngine::PVSParams::ShapeType::Frustum;
		params.camera = camera;
		cameraPvs->CalculateVisibility(scene, params);
	}
}

namespace HexEngine
{
	//const int32_t IconCanvasSize = 400;

	void IconService::Create(const std::wstring& sceneName)
	{
		_iconScene = g_pEnv->_sceneManager->CreateEmptyScene(false, nullptr, true);
		_iconScene->SetFlags(SceneFlags::Utility);
		_iconScene->CreateDefaultSunLight();
		if (auto* sun = _iconScene->GetSunLight(); sun != nullptr && sun->GetEntity() != nullptr)
		{
			sun->GetEntity()->SetFlag(EntityFlags::DoNotSave);
		}
		_iconScene->SetName(sceneName);
		g_pEnv->_debugGui->RemoveCallback(_iconScene.get());

		uint32_t width, height;
		g_pEnv->_graphicsDevice->GetBackBufferDimensions(width, height);

		auto cameraEnt = _iconScene->CreateEntity("IconCamera", math::Vector3(0.0f, 0.0f, -50.0f));
		cameraEnt->SetFlag(EntityFlags::DoNotSave);
		_camera = cameraEnt->AddComponent<Camera>();
		_camera->SetPespectiveParameters(60.0f, 1.0f, 1.0f, 2000.0f);
		_camera->SetViewport(math::Viewport(0.0f, 0.0f, (float)width, (float)height));

		_previewRootEntities.clear();
		
	}

	void IconService::Destroy()
	{
		ClearPreviewEntities();
		//g_pEnv->_sceneManager->UnloadScene(_iconScene.get());
	}

	void IconService::ClearPreviewEntities()
	{
		if (_iconScene == nullptr)
		{
			_previewRootEntities.clear();
			return;
		}

		_iconScene->Update(0.0f);

		auto* cameraEntity = _camera ? _camera->GetEntity() : nullptr;
		auto* sunEntity = _iconScene->GetSunLight() ? _iconScene->GetSunLight()->GetEntity() : nullptr;

		// Sweep the preview scene directly so we never rely on stale tracked roots.
		for (int32_t pass = 0; pass < 8; ++pass)
		{
			std::vector<Entity*> candidates;
			CollectSceneEntities(_iconScene.get(), candidates, cameraEntity, sunEntity);
			if (candidates.empty())
				break;

			std::unordered_set<Entity*> candidateSet(candidates.begin(), candidates.end());
			std::vector<Entity*> rootsToDestroy;
			rootsToDestroy.reserve(candidates.size());

			// Destroy roots first; children are removed recursively by Scene::DestroyEntity.
			for (auto* entity : candidates)
			{
				if (entity == nullptr || entity->IsPendingDeletion())
					continue;

				auto* parent = entity->GetParent();
				if (parent != nullptr && candidateSet.find(parent) != candidateSet.end())
					continue;

				rootsToDestroy.push_back(entity);
			}

			if (rootsToDestroy.empty())
				break;

			for (auto* entity : rootsToDestroy)
			{
				if (entity == nullptr || entity->IsPendingDeletion())
					continue;

				if (entity->GetScene() == _iconScene.get())
				{
					_iconScene->DestroyEntity(entity);
				}
			}
		}

		_previewRootEntities.clear();
	}

	void IconService::PushFilePathForIconGeneration(const fs::path& path)
	{
		auto it = _icons.find(path);
		auto itRes = _resourceIcons.find(path);

		// Only add a request if the icon hasn't already been generated
		if (it == _icons.end() && itRes == _resourceIcons.end())
		{
			LOG_INFO("IconService: An icon was requested for: %s", path.string().c_str());

			IconPending pending;
			pending.path = path;
			_pendingPaths.push_back(pending);
		}
	}

	void IconService::PushAssetPathForIconGeneration(const std::wstring& assetPackage, const fs::path& assetPath)
	{
		auto it = _icons.find(assetPath);
		auto itRes = _resourceIcons.find(assetPath);

		// Only add a request if the icon hasn't already been generated
		if (it == _icons.end() && itRes == _resourceIcons.end())
		{
			LOG_INFO("IconService: An icon was requested for: %s", assetPath.string().c_str());

			IconPending pending;
			pending.path = assetPath;
			pending.isAsset = true;
			_pendingPaths.push_back(pending);
		}
	}

	void IconService::Render()
	{
		if (_pendingPaths.size() > 0)
		{
			_iconScene->SetFlags(SceneFlags::Renderable);

			g_pEnv->GetGraphicsDevice().SetClearColour(math::Color(HEX_RGB_TO_FLOAT3(40, 44, 48)));

			const auto& pending = _pendingPaths[0];

			auto path = pending.path;
			auto extension = path.extension().string();

			std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

			// Textures can be used directly as icons; no preview scene render required.
			if (IsImageExtension(extension))
			{
				auto texture = ITexture2D::Create(path);
				if (texture != nullptr)
				{
					_resourceIcons[path] = texture;
				}

				_pendingPaths.erase(_pendingPaths.begin());
				return;
			}

			g_pEnv->_sceneManager->SetActiveScene(_iconScene);
			_camera->GetRenderTarget()->ClearRenderTargetView(math::Color(HEX_RGB_TO_FLOAT3(40, 44, 48)));
			ClearPreviewEntities();

			bool hasPreviewEntities = false;

			if (extension == ".hmesh")
			{
				auto* dummyEnt = _iconScene->CreateEntity("DummyEnt");
				dummyEnt->AddComponent<StaticMeshComponent>();
				auto* meshRenderer = dummyEnt->GetComponent<StaticMeshComponent>();

				auto mesh = Mesh::Create(path);
				if (mesh != nullptr)
				{
					meshRenderer->SetMesh(mesh);
					_previewRootEntities.push_back(dummyEnt);
					hasPreviewEntities = true;
				}
				else
				{
					_iconScene->DestroyEntity(dummyEnt);
				}
			}
			else if (extension == ".hmat")
			{
				auto* dummyEnt = _iconScene->CreateEntity("DummyEnt");
				dummyEnt->AddComponent<StaticMeshComponent>();
				auto* meshRenderer = dummyEnt->GetComponent<StaticMeshComponent>();

				auto mesh = Mesh::Create("EngineData.Models/Primitives/sphere.hmesh");
				if (mesh != nullptr)
				{
					meshRenderer->SetMesh(mesh);
					meshRenderer->SetMaterial(Material::Create(path));
					_previewRootEntities.push_back(dummyEnt);
					hasPreviewEntities = true;
				}
				else
				{
					_iconScene->DestroyEntity(dummyEnt);
				}
			}
			else if (extension == ".hprefab")
			{
				auto* cameraEntity = _camera ? _camera->GetEntity() : nullptr;
				auto* sunEntity = _iconScene->GetSunLight() ? _iconScene->GetSunLight()->GetEntity() : nullptr;

				std::vector<Entity*> existingEntities;
				CollectSceneEntities(_iconScene.get(), existingEntities, cameraEntity, sunEntity);
				std::unordered_set<Entity*> existingEntitySet(existingEntities.begin(), existingEntities.end());

				if (g_pEnv->_prefabLoader->LoadPrefabAssetToScene(path, _iconScene))
				{
					std::vector<Entity*> loadedEntities;
					CollectSceneEntities(_iconScene.get(), loadedEntities, cameraEntity, sunEntity);

					std::vector<Entity*> newlyAddedEntities;
					newlyAddedEntities.reserve(loadedEntities.size());
					for (auto* entity : loadedEntities)
					{
						if (existingEntitySet.find(entity) == existingEntitySet.end())
						{
							newlyAddedEntities.push_back(entity);
						}
					}

					_previewRootEntities = CollectUniquePrefabRoots(newlyAddedEntities);
					hasPreviewEntities = !_previewRootEntities.empty();
				}
			}

			if (hasPreviewEntities)
			{
				if (!FrameCameraToPreviewRoots(_camera, _previewRootEntities))
				{
					if (!SceneFramingUtils::FrameCameraToSceneBounds(_iconScene.get(), _camera, true))
					{
						SceneFramingUtils::FrameCameraToSceneBounds(_iconScene.get(), _camera, false);
					}
				}
				SeedPVSForPreview(_iconScene.get(), _camera, _previewRootEntities);
				_generatedPaths.push_back(path);
			}
			else
			{
				if (auto* pvs = _camera->GetPVS(); pvs != nullptr)
				{
					pvs->ClearPVS();
				}

				if (auto* sun = _iconScene->GetSunLight(); sun != nullptr)
				{
					for (int32_t i = 0; i < sun->GetMaxSupportedShadowCascades(); ++i)
					{
						sun->GetPVS(i)->ClearPVS();
					}
				}
			}

			_pendingPaths.erase(_pendingPaths.begin());
		}
		else
		{
			_iconScene->SetFlags(SceneFlags::Utility);

			_camera->GetPVS()->ClearPVS();

			for (int32_t i = 0; i < _iconScene->GetSunLight()->GetMaxSupportedShadowCascades(); ++i)
			{
				_iconScene->GetSunLight()->GetPVS(i)->ClearPVS();
			}
		}
	}

	void IconService::CompletedFrame()
	{
		if (_generatedPaths.size() > 0)
		{
			auto lastScene = g_pEnv->_sceneManager->GetCurrentScene();
			g_pEnv->_sceneManager->SetActiveScene(_iconScene);

			auto path = _generatedPaths[0];

			auto cameraRT = _camera->GetRenderTarget();

			GFX_PERF_BEGIN(0xffffffff, L"Rendering IconScene");

			/*if (_icons.find(path) != _icons.end())
			{
				DebugBreak();
			}*/

			_icons[path] = g_pEnv->_graphicsDevice->CreateTexture2D(
				512,
				512,
				DXGI_FORMAT_R8G8B8A8_UNORM,
				1, 
				D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE,
				0, 1, 0, 
				nullptr,
				(D3D11_CPU_ACCESS_FLAG)0,
				D3D11_RTV_DIMENSION_TEXTURE2D,
				D3D11_UAV_DIMENSION_UNKNOWN,
				D3D11_SRV_DIMENSION_TEXTURE2D);

			_icons[path]->ClearRenderTargetView(math::Color(1, 0, 0, 1));
			g_pEnv->_graphicsDevice->SetRenderTarget(_icons[path]);

			g_pEnv->_graphicsDevice->SetViewport(*math::Viewport(0, 0, 512, 512).Get11());

			auto renderer = g_pEnv->GetUIManager().GetRenderer();
			{
				renderer->StartFrame(512, 512);
				renderer->FullScreenTexturedQuad(cameraRT);
				renderer->EndFrame();
				//_camera->GetRenderTarget()->CopyTo(_icons[path]);
			}

			GFX_PERF_END();

			_generatedPaths.erase(_generatedPaths.begin());
			g_pEnv->_sceneManager->SetActiveScene(lastScene);
		}
		else
		{
			_iconScene->SetFlags(SceneFlags::Utility);

			_camera->GetPVS()->ClearPVS();

			for (int32_t i = 0; i < _iconScene->GetSunLight()->GetMaxSupportedShadowCascades(); ++i)
			{
				_iconScene->GetSunLight()->GetPVS(i)->ClearPVS();
			}
			ClearPreviewEntities();
		}
	}

	ITexture2D* IconService::GetIcon(const fs::path& path)
	{
		if (auto itRes = _resourceIcons.find(path); itRes != _resourceIcons.end())
		{
			return itRes->second.get();
		}

		auto it = _icons.find(path);

		if (it == _icons.end())
			return nullptr;

		return it->second;
	}

	void IconService::RemoveIcon(const fs::path& path)
	{
		auto itRes = _resourceIcons.find(path);
		if (itRes != _resourceIcons.end())
		{
			_resourceIcons.erase(itRes);
		}

		auto it = _icons.find(path);

		if (it == _icons.end())
			return;

		_icons.erase(it);
	}
}

