

#include "IconService.hpp"

namespace HexEngine
{
	//const int32_t IconCanvasSize = 400;

	void IconService::Create(const std::wstring& sceneName)
	{
		_iconScene = g_pEnv->_sceneManager->CreateEmptyScene(false, nullptr, true);
		_iconScene->SetFlags(SceneFlags::Utility);
		_iconScene->CreateDefaultSunLight();
		_iconScene->SetName(sceneName);
		g_pEnv->_debugGui->RemoveCallback(_iconScene.get());

		uint32_t width, height;
		g_pEnv->_graphicsDevice->GetBackBufferDimensions(width, height);

		auto cameraEnt = _iconScene->CreateEntity("IconCamera", math::Vector3(0.0f, 0.0f, -50.0f));
		_camera = cameraEnt->AddComponent<Camera>();
		_camera->SetPespectiveParameters(90.0f, 1.0f, 1.0f, 2000.0f);
		_camera->SetViewport(math::Viewport(0.0f, 0.0f, (float)width, (float)height));

		
	}

	void IconService::Destroy()
	{
		//g_pEnv->_sceneManager->UnloadScene(_iconScene.get());
	}

	void IconService::PushFilePathForIconGeneration(const fs::path& path)
	{
		auto it = _icons.find(path);

		// Only add a request if the icon hasn't already been generated
		if (it == _icons.end())
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

		// Only add a request if the icon hasn't already been generated
		if (it == _icons.end())
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

			const auto& pending = _pendingPaths[0];

			auto path = pending.path;
			auto extension = path.extension().string();

			std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

			if (extension == ".hmesh")
			{
				g_pEnv->_sceneManager->SetActiveScene(_iconScene);
				_camera->GetRenderTarget()->ClearRenderTargetView(math::Color(0.1f, 0.1f, 0.1f, 1.0f));

				if (_dummyEnt != nullptr)
				{
					_iconScene->DestroyEntity(_dummyEnt);
				}
				_dummyEnt = _iconScene->CreateEntity("DummyEnt");
				_dummyEnt->AddComponent<StaticMeshComponent>();
				
				auto meshRenderer = _dummyEnt->GetComponent<StaticMeshComponent>();

				//Model::Create(path);
				
				//, [this, meshRenderer, path](IResource* resource) {
				{

					auto model = Mesh::Create(path);// (Model*)resource;

					if (model)
					{
						meshRenderer->SetMesh(model);
					}

					auto pos = math::Vector3(0, _dummyEnt->GetAABB().Center.y, _dummyEnt->GetBoundingSphere().Radius * 1.05f);

					_camera->GetEntity()->SetPosition(pos);

					_camera->SetLookDirection((_dummyEnt->GetAABB().Center - _camera->GetEntity()->GetPosition()), math::Vector3::Up);
					//_camera->SetYaw(-4.0f);
					//_camera->SetPitch(-20.0f);

					//ITexture2D* renderTarget = g_pEnv->_graphicsDevice->CreateTexture(_camera->GetRenderTarget());

					//_camera->GetEntity()->GetComponent<Transform>()->SetRotation(math::Quaternion::FromToRotation(pos, _dummyEnt->GetWorldAABB().Center));

					//_camera->GetEntity()->GetComponent<Transform>()->SetRotation(math::Quaternion::FromToRotation(_camera->GetEntity()->GetPosition(), dummyEnt->GetPosition()));

					_camera->GetPVS()->ClearPVS();
					_camera->GetPVS()->AddEntity(_dummyEnt);


					for (int32_t i = 0; i < _iconScene->GetSunLight()->GetMaxSupportedShadowCascades(); ++i)
					{
						_iconScene->GetSunLight()->GetPVS(i)->ClearPVS();
						_iconScene->GetSunLight()->GetPVS(i)->AddEntity(_dummyEnt);
					}

					PVSParams params;
					params.lodPartition = _camera->GetFarZ();
					params.shape.frustum.sm = _camera->GetFrustum();
					params.shapeType = PVSParams::ShapeType::Frustum;
					params.camera = _camera;
					_camera->GetPVS()->CalculateVisibility(_iconScene.get(), params);

					_generatedPaths.push_back(path);
				}
			}
			else if (false && extension == ".hmat")
			{
				g_pEnv->_sceneManager->SetActiveScene(_iconScene);

				_camera->GetRenderTarget()->ClearRenderTargetView(math::Color(0.1f, 0.1f, 0.1f, 1.0f));


				auto meshRenderer = _dummyEnt->GetComponent<StaticMeshComponent>();

				meshRenderer->ReleaseAllMeshes();

				auto mesh = Mesh::Create("EngineData.Models/Primitives/sphere.hmesh");

				if (mesh)
				{
					meshRenderer->SetMesh(mesh);

					meshRenderer->SetMaterial(Material::Create(path));
				}

				_camera->GetEntity()->SetPosition(math::Vector3(0, 0, _dummyEnt->GetBoundingSphere().Radius * 1.2f));

				_camera->SetYaw(0.0f);
				_camera->SetPitch(-45.0f);
				//_camera->GetEntity()->GetComponent<Transform>()->SetRotation(math::Quaternion::FromToRotation(_camera->GetEntity()->GetPosition(), dummyEnt->GetPosition()));

				_camera->GetPVS()->ClearPVS();
				_camera->GetPVS()->AddEntity(_dummyEnt);


				for (int32_t i = 0; i < _iconScene->GetSunLight()->GetMaxSupportedShadowCascades(); ++i)
				{
					_iconScene->GetSunLight()->GetPVS(i)->ClearPVS();
					_iconScene->GetSunLight()->GetPVS(i)->AddEntity(_dummyEnt);
				}

				PVSParams params;
				params.lodPartition = _camera->GetFarZ();
				params.shape.frustum.sm = _camera->GetFrustum();
				params.shapeType = PVSParams::ShapeType::Frustum;
				params.camera = _camera;
				_camera->GetPVS()->CalculateVisibility(_iconScene.get(), params);

				_generatedPaths.push_back(path);
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

			if (_icons.find(path) != _icons.end())
			{
				DebugBreak();
			}

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

			if (_dummyEnt)
			{
				_iconScene->DestroyEntity(_dummyEnt);
				_dummyEnt = nullptr;
			}
		}
	}

	ITexture2D* IconService::GetIcon(const fs::path& path)
	{
		auto it = _icons.find(path);

		if (it == _icons.end())
			return nullptr;

		return it->second;
	}

	void IconService::RemoveIcon(const fs::path& path)
	{
		auto it = _icons.find(path);

		if (it == _icons.end())
			return;

		_icons.erase(it);
	}
}