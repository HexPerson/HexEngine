

#include "IconService.hpp"
#include "../Scene/PVS.hpp"
#include "../Scene/SceneFramingUtils.hpp"

#include <algorithm>
#include <cctype>
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

		for (auto* entity : _previewRootEntities)
		{
			if (entity == nullptr || entity->IsPendingDeletion())
				continue;

			if (entity->GetScene() == _iconScene.get())
			{
				_iconScene->DestroyEntity(entity);
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
				auto spawnedEntities = g_pEnv->_sceneManager->LoadPrefab(_iconScene, path);
				_previewRootEntities = CollectUniquePrefabRoots(spawnedEntities);
				hasPreviewEntities = !_previewRootEntities.empty();
			}

			if (hasPreviewEntities)
			{
				SceneFramingUtils::FrameCameraToSceneBounds(_iconScene.get(), _camera, true);
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
				renderer->StartFrame(512,512);
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
