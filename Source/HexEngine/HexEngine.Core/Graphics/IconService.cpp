

#include "IconService.hpp"
#include "../FileSystem/PrefabLoader.hpp"
#include "../Scene/PVS.hpp"
#include "../Scene/SceneFramingUtils.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <limits>
#include <unordered_set>

constexpr int32_t kIconSize = 1024;
constexpr size_t kMaxAsyncMeshPreviewLoads = 3;
constexpr size_t kMaxResidentGeneratedIcons = 512;
constexpr int32_t kDiskCacheVersion = 1;

namespace
{
	fs::path NormalizeIconPath(const fs::path& input)
	{
		if (input.empty())
			return {};

		fs::path normalized = input.lexically_normal();
		try
		{
			if (fs::exists(normalized))
				normalized = fs::weakly_canonical(normalized);
		}
		catch (...)
		{
		}

#ifdef _WIN32
		std::wstring lower = normalized.make_preferred().wstring();
		std::transform(lower.begin(), lower.end(), lower.begin(),
			[](wchar_t c) { return static_cast<wchar_t>(::towlower(c)); });
		return fs::path(lower);
#else
		return normalized.make_preferred();
#endif
	}

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

	int64_t ToFileTimeTicks(const fs::file_time_type& time)
	{
		return time.time_since_epoch().count();
	}

}

namespace HexEngine
{
	//const int32_t IconCanvasSize = 400;

	int64_t IconService::GetFileWriteTimeTicks(const fs::path& path)
	{
		std::error_code ec;
		auto time = fs::last_write_time(path, ec);
		if (ec)
			return 0;

		return ToFileTimeTicks(time);
	}

	uintmax_t IconService::GetFileSizeSafe(const fs::path& path)
	{
		std::error_code ec;
		const auto fileSize = fs::file_size(path, ec);
		if (ec)
			return 0;

		return fileSize;
	}

	std::string IconService::BuildCacheFileName(const fs::path& normalizedPath)
	{
		const auto input = normalizedPath.generic_string();
		const uint64_t h = std::hash<std::string>{}(input);
		char buffer[32] = {};
		snprintf(buffer, sizeof(buffer), "%016llx.png", static_cast<unsigned long long>(h));
		return std::string(buffer);
	}

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

		auto cameraEnt = _iconScene->CreateEntity("IconCamera", math::Vector3(0.0f, 0.0f, -50.0f));
		cameraEnt->SetFlag(EntityFlags::DoNotSave);
		_camera = cameraEnt->AddComponent<Camera>();
		_iconScene->SetMainCamera(_camera);
		_camera->SetPespectiveParameters(60.0f, 1.0f, 1.0f, 2000.0f);
		_camera->SetViewport(math::Viewport(0.0f, 0.0f, (float)kIconSize, (float)kIconSize));

		_previewRootEntities.clear();

		_diskCacheRoot = g_pEnv->GetFileSystem().GetLocalAbsolutePath(fs::path(L"Data/Cache/Icons"));
		_diskCacheIndexFile = _diskCacheRoot / fs::path(L"index.json");
		std::error_code ec;
		fs::create_directories(_diskCacheRoot, ec);
		LoadDiskCacheIndex();
		
	}

	void IconService::Destroy()
	{
		ClearPreviewEntities();
		_pendingPaths.clear();
		_queuedPaths.clear();
		for (auto& [_, icon] : _icons)
		{
			SAFE_DELETE(icon);
		}
		_icons.clear();
		_resourceIcons.clear();
		_iconLru.clear();
		_iconLruIndex.clear();
		SaveDiskCacheIndex();
		//g_pEnv->_sceneManager->UnloadScene(_iconScene.get());
	}

	void IconService::LoadDiskCacheIndex()
	{
		_diskCacheIndex.clear();
		_diskCacheDirty = false;

		if (_diskCacheIndexFile.empty() || !fs::exists(_diskCacheIndexFile))
			return;

		std::ifstream file(_diskCacheIndexFile, std::ios::in | std::ios::binary);
		if (!file.is_open())
			return;

		json root;
		try
		{
			file >> root;
		}
		catch (...)
		{
			return;
		}

		if (!root.contains("version") || root["version"].get<int32_t>() != kDiskCacheVersion)
			return;

		if (!root.contains("entries") || !root["entries"].is_array())
			return;

		for (const auto& item : root["entries"])
		{
			if (!item.is_object())
				continue;
			if (!item.contains("source") || !item.contains("cache"))
				continue;

			const fs::path source = NormalizeIconPath(item["source"].get<std::string>());
			if (source.empty())
				continue;

			IconDiskCacheEntry entry;
			entry.cacheFileName = item["cache"].get<std::string>();
			if (item.contains("writeTime"))
				entry.sourceWriteTime = item["writeTime"].get<int64_t>();
			if (item.contains("fileSize"))
				entry.sourceFileSize = item["fileSize"].get<uintmax_t>();

			_diskCacheIndex[source] = std::move(entry);
		}
	}

	void IconService::SaveDiskCacheIndex()
	{
		if (!_diskCacheDirty || _diskCacheIndexFile.empty())
			return;

		std::error_code ec;
		fs::create_directories(_diskCacheRoot, ec);

		json root = json::object();
		root["version"] = kDiskCacheVersion;
		root["entries"] = json::array();

		for (const auto& [sourcePath, entry] : _diskCacheIndex)
		{
			json item = json::object();
			item["source"] = sourcePath.generic_string();
			item["cache"] = entry.cacheFileName;
			item["writeTime"] = entry.sourceWriteTime;
			item["fileSize"] = entry.sourceFileSize;
			root["entries"].push_back(std::move(item));
		}

		std::ofstream file(_diskCacheIndexFile, std::ios::out | std::ios::trunc | std::ios::binary);
		if (!file.is_open())
			return;

		file << root.dump(1, '\t');
		_diskCacheDirty = false;
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

	void IconService::BeginAsyncMeshLoad(IconPending& pending)
	{
		pending.state = IconPending::State::LoadingMeshAsync;
		const fs::path key = pending.path;

		Mesh::CreateAsync(key, [this, key](std::shared_ptr<IResource> resource)
		{
			auto* request = FindPendingByPath(key);
			if (request == nullptr)
				return;

			auto mesh = std::dynamic_pointer_cast<Mesh>(resource);
			if (mesh != nullptr)
			{
				request->loadedMesh = mesh;
				request->state = IconPending::State::ReadyToRender;
			}
			else
			{
				request->state = IconPending::State::Failed;
			}
		});
	}

	void IconService::PumpAsyncMeshLoads()
	{
		size_t activeMeshLoads = 0;
		for (const auto& pending : _pendingPaths)
		{
			if (pending.extensionLower == ".hmesh" && pending.state == IconPending::State::LoadingMeshAsync)
			{
				++activeMeshLoads;
			}
		}

		if (activeMeshLoads >= kMaxAsyncMeshPreviewLoads)
			return;

		for (auto& pending : _pendingPaths)
		{
			if (activeMeshLoads >= kMaxAsyncMeshPreviewLoads)
				break;

			if (pending.extensionLower != ".hmesh")
				continue;

			if (pending.state != IconPending::State::Queued)
				continue;

			BeginAsyncMeshLoad(pending);
			++activeMeshLoads;
		}
	}

	IconPending* IconService::FindPendingByPath(const fs::path& path)
	{
		for (auto& pending : _pendingPaths)
		{
			if (pending.path == path)
				return &pending;
		}
		return nullptr;
	}

	void IconService::RemovePendingByPath(const fs::path& path)
	{
		for (auto it = _pendingPaths.begin(); it != _pendingPaths.end(); ++it)
		{
			if (it->path == path)
			{
				_pendingPaths.erase(it);
				return;
			}
		}
	}

	bool IconService::PopNextRenderablePending(IconPending& outPending)
	{
		for (auto it = _pendingPaths.begin(); it != _pendingPaths.end(); ++it)
		{
			if (it->state == IconPending::State::LoadingMeshAsync)
				continue;
			if (it->extensionLower == ".hmesh" && it->state == IconPending::State::Queued)
				continue;

			outPending = std::move(*it);
			_pendingPaths.erase(it);
			return true;
		}

		return false;
	}

	void IconService::TouchIconLru(const fs::path& path)
	{
		auto it = _iconLruIndex.find(path);
		if (it != _iconLruIndex.end())
		{
			_iconLru.erase(it->second);
			_iconLruIndex.erase(it);
		}

		_iconLru.push_back(path);
		auto newIt = _iconLru.end();
		--newIt;
		_iconLruIndex[path] = newIt;
	}

	void IconService::EnforceIconMemoryBudget()
	{
		while (_icons.size() > kMaxResidentGeneratedIcons && !_iconLru.empty())
		{
			const fs::path victim = _iconLru.front();
			_iconLru.pop_front();
			_iconLruIndex.erase(victim);

			auto it = _icons.find(victim);
			if (it == _icons.end())
				continue;

			SAFE_DELETE(it->second);
			_icons.erase(it);
		}
	}

	void IconService::SaveIconToDiskCache(const fs::path& sourcePath, ITexture2D* texture)
	{
		if (texture == nullptr || sourcePath.empty() || _diskCacheRoot.empty())
			return;

		const std::string cacheFile = BuildCacheFileName(sourcePath);
		const fs::path cachePath = _diskCacheRoot / fs::path(cacheFile);
		const fs::path baseDir = g_pEnv->GetFileSystem().GetBaseDirectory();
		std::error_code ec;
		fs::path cachePathRelative = fs::relative(cachePath, baseDir, ec);
		if (ec || cachePathRelative.empty())
			cachePathRelative = fs::path(L"Data/Cache/Icons") / fs::path(cacheFile);

		texture->SaveToFile(cachePathRelative);

		IconDiskCacheEntry entry;
		entry.cacheFileName = cacheFile;
		entry.sourceWriteTime = GetFileWriteTimeTicks(sourcePath);
		entry.sourceFileSize = GetFileSizeSafe(sourcePath);
		_diskCacheIndex[sourcePath] = std::move(entry);
		_diskCacheDirty = true;
	}

	bool IconService::TryLoadIconFromDiskCache(const fs::path& sourcePath)
	{
		if (sourcePath.empty())
			return false;

		auto itEntry = _diskCacheIndex.find(sourcePath);
		if (itEntry == _diskCacheIndex.end())
			return false;

		const auto& entry = itEntry->second;
		const int64_t currentWriteTime = GetFileWriteTimeTicks(sourcePath);
		const uintmax_t currentFileSize = GetFileSizeSafe(sourcePath);
		if (currentWriteTime == 0 || currentFileSize == 0 ||
			entry.sourceWriteTime != currentWriteTime ||
			entry.sourceFileSize != currentFileSize)
		{
			RemoveIconFromDiskCache(sourcePath);
			return false;
		}

		const fs::path cachePath = _diskCacheRoot / fs::path(entry.cacheFileName);
		if (!fs::exists(cachePath))
		{
			RemoveIconFromDiskCache(sourcePath);
			return false;
		}

		auto tex = ITexture2D::Create(cachePath);
		if (tex == nullptr)
		{
			RemoveIconFromDiskCache(sourcePath);
			return false;
		}

		const int32_t texWidth = std::max(1, tex->GetWidth());
		const int32_t texHeight = std::max(1, tex->GetHeight());
		const DXGI_FORMAT texFormat = static_cast<DXGI_FORMAT>(tex->GetFormat());

		auto* clone = g_pEnv->_graphicsDevice->CreateTexture2D(
			texWidth,
			texHeight,
			texFormat == DXGI_FORMAT_UNKNOWN ? DXGI_FORMAT_R8G8B8A8_UNORM : texFormat,
			1,
			D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
			0, 1, 0,
			nullptr,
			(D3D11_CPU_ACCESS_FLAG)0,
			D3D11_RTV_DIMENSION_TEXTURE2D,
			D3D11_UAV_DIMENSION_UNKNOWN,
			D3D11_SRV_DIMENSION_TEXTURE2D);
		if (clone == nullptr)
			return false;

		const RECT srcRect = { 0, 0, texWidth, texHeight };
		const RECT dstRect = { 0, 0, texWidth, texHeight };
		tex->CopyTo(clone, srcRect, dstRect);

		_icons[sourcePath] = clone;
		TouchIconLru(sourcePath);
		EnforceIconMemoryBudget();
		return true;
	}

	void IconService::RemoveIconFromDiskCache(const fs::path& sourcePath)
	{
		auto it = _diskCacheIndex.find(sourcePath);
		if (it == _diskCacheIndex.end())
			return;

		if (!it->second.cacheFileName.empty() && !_diskCacheRoot.empty())
		{
			const fs::path cachePath = _diskCacheRoot / fs::path(it->second.cacheFileName);
			std::error_code ec;
			fs::remove(cachePath, ec);
		}

		_diskCacheIndex.erase(it);
		_diskCacheDirty = true;
	}

	void IconService::PushFilePathForIconGeneration(const fs::path& path)
	{
		const fs::path key = NormalizeIconPath(path);
		auto it = _icons.find(key);
		auto itRes = _resourceIcons.find(key);
		auto itQueued = _queuedPaths.find(key);

		if (it != _icons.end())
		{
			TouchIconLru(key);
			return;
		}
		if (itRes != _resourceIcons.end())
		{
			return;
		}

		if (TryLoadIconFromDiskCache(key))
			return;

		// Only add a request if the icon hasn't already been generated or queued.
		if (itQueued == _queuedPaths.end())
		{
			LOG_INFO("IconService: An icon was requested for: %s", key.string().c_str());

			IconPending pending;
			pending.path = key;
			pending.extensionLower = key.extension().string();
			std::transform(pending.extensionLower.begin(), pending.extensionLower.end(), pending.extensionLower.begin(), ::tolower);
			_pendingPaths.push_back(std::move(pending));
			_queuedPaths.insert(key);
		}
	}

	void IconService::PushAssetPathForIconGeneration(const std::wstring& assetPackage, const fs::path& assetPath)
	{
		const fs::path key = NormalizeIconPath(assetPath);
		auto it = _icons.find(key);
		auto itRes = _resourceIcons.find(key);
		auto itQueued = _queuedPaths.find(key);

		if (it != _icons.end())
		{
			TouchIconLru(key);
			return;
		}
		if (itRes != _resourceIcons.end())
		{
			return;
		}

		if (TryLoadIconFromDiskCache(key))
			return;

		// Only add a request if the icon hasn't already been generated or queued.
		if (itQueued == _queuedPaths.end())
		{
			LOG_INFO("IconService: An icon was requested for: %s", key.string().c_str());

			IconPending pending;
			pending.path = key;
			pending.isAsset = true;
			pending.assetPackage = assetPackage;
			pending.extensionLower = key.extension().string();
			std::transform(pending.extensionLower.begin(), pending.extensionLower.end(), pending.extensionLower.begin(), ::tolower);
			_pendingPaths.push_back(std::move(pending));
			_queuedPaths.insert(key);
		}
	}

	void IconService::Render()
	{
		if (_pendingPaths.size() > 0)
		{
			PumpAsyncMeshLoads();

			IconPending pending;
			if (!PopNextRenderablePending(pending))
			{
				_iconScene->SetFlags(SceneFlags::Utility);
				if (_camera && _camera->GetPVS())
				{
					_camera->GetPVS()->ClearPVS();
				}
				if (auto* sun = _iconScene->GetSunLight(); sun != nullptr)
				{
					for (int32_t i = 0; i < sun->GetMaxSupportedShadowCascades(); ++i)
					{
						sun->GetPVS(i)->ClearPVS();
					}
				}
				return;
			}

			_iconScene->SetFlags(SceneFlags::Renderable);

			g_pEnv->GetGraphicsDevice().SetClearColour(math::Color(HEX_RGB_TO_FLOAT3(40, 44, 48)));

			const auto path = pending.path;
			std::string extension = pending.extensionLower;
			if (extension.empty())
			{
				extension = path.extension().string();
				std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
			}

			// Textures can be used directly as icons; no preview scene render required.
			if (IsImageExtension(extension))
			{
				auto texture = ITexture2D::Create(path);
				if (texture != nullptr)
				{
					_resourceIcons[NormalizeIconPath(path)] = texture;
				}

				_queuedPaths.erase(path);
				return;
			}

			g_pEnv->_sceneManager->SetActiveScene(_iconScene);
			_camera->GetRenderTarget()->ClearRenderTargetView(math::Color(HEX_RGB_TO_FLOAT3(40, 44, 48)));
			ClearPreviewEntities();

			bool hasPreviewEntities = false;

			if (extension == ".hmesh")
			{
				auto mesh = pending.loadedMesh;
				if (mesh != nullptr)
				{
					auto* dummyEnt = _iconScene->CreateEntity("DummyEnt");
					dummyEnt->AddComponent<StaticMeshComponent>();
					auto* meshRenderer = dummyEnt->GetComponent<StaticMeshComponent>();

					meshRenderer->SetMesh(mesh);
					_previewRootEntities.push_back(dummyEnt);
					hasPreviewEntities = true;
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

				// Render and capture immediately so icon generation does not rely on external frame ordering.
				g_pEnv->_sceneRenderer->RenderScene(_iconScene.get(), _camera, _iconScene->GetFlags());

				auto* cameraRT = _camera->GetRenderTarget();
				if (cameraRT != nullptr)
				{
					auto* iconTexture = g_pEnv->_graphicsDevice->CreateTexture2D(
						kIconSize,
						kIconSize,
						DXGI_FORMAT_R8G8B8A8_UNORM,
						1,
						D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
						0, 1, 0,
						nullptr,
						(D3D11_CPU_ACCESS_FLAG)0,
						D3D11_RTV_DIMENSION_TEXTURE2D,
						D3D11_UAV_DIMENSION_UNKNOWN,
						D3D11_SRV_DIMENSION_TEXTURE2D);

					if (iconTexture != nullptr)
					{
						// Keep icon alpha opaque so UI thumbnail blending does not hide valid RGB.
						iconTexture->ClearRenderTargetView(math::Color(0, 0, 0, 1));
						cameraRT->BlendTo_Additive(iconTexture, nullptr);

						if (auto it = _icons.find(path); it != _icons.end())
						{
							SAFE_DELETE(it->second);
							it->second = iconTexture;
						}
						else
						{
							_icons[path] = iconTexture;
						}
						TouchIconLru(path);
						EnforceIconMemoryBudget();
						SaveIconToDiskCache(path, iconTexture);
					}
				}
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

			_queuedPaths.erase(path);
		}
		else
		{
			SaveDiskCacheIndex();

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
				kIconSize,
				kIconSize,
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

			g_pEnv->_graphicsDevice->SetViewport(*math::Viewport(0, 0, kIconSize, kIconSize).Get11());

			auto renderer = g_pEnv->GetUIManager().GetRenderer();
			{
				renderer->StartFrame(kIconSize, kIconSize);
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
		const fs::path key = NormalizeIconPath(path);

		if (auto itRes = _resourceIcons.find(key); itRes != _resourceIcons.end())
		{
			return itRes->second.get();
		}

		auto it = _icons.find(key);
		if (it != _icons.end())
		{
			TouchIconLru(key);
			return it->second;
		}

		if (TryLoadIconFromDiskCache(key))
		{
			auto itDisk = _icons.find(key);
			if (itDisk != _icons.end())
			{
				return itDisk->second;
			}
		}

		return nullptr;
	}

	void IconService::RemoveIcon(const fs::path& path)
	{
		const fs::path key = NormalizeIconPath(path);
		_queuedPaths.erase(key);
		RemovePendingByPath(key);
		RemoveIconFromDiskCache(key);

		auto itRes = _resourceIcons.find(key);
		if (itRes != _resourceIcons.end())
		{
			_resourceIcons.erase(itRes);
		}

		auto it = _icons.find(key);

		if (it == _icons.end())
			return;

		SAFE_DELETE(it->second);
		_icons.erase(it);
		auto itLru = _iconLruIndex.find(key);
		if (itLru != _iconLruIndex.end())
		{
			_iconLru.erase(itLru->second);
			_iconLruIndex.erase(itLru);
		}
	}
}

