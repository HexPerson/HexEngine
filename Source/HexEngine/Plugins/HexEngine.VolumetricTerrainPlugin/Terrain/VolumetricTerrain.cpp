#include "VolumetricTerrain.hpp"
#include <limits>
#include <unordered_set>

namespace HexEngine::VolumetricTerrain
{
	namespace
	{
		Scene* GetTerrainScene(Entity* owner, Entity* rootEntity)
		{
			if (owner != nullptr && owner->GetScene() != nullptr)
			{
				return owner->GetScene();
			}

			if (rootEntity != nullptr && rootEntity->GetScene() != nullptr)
			{
				return rootEntity->GetScene();
			}

			return nullptr;
		}
	}

	VolumetricTerrain::VolumetricTerrain()
	{
	}

	VolumetricTerrain::~VolumetricTerrain()
	{
		if (_owner != nullptr && _owner->GetScene() != nullptr)
		{
			_owner->GetScene()->UnregisterCustomRenderer(this);
		}

		if (_rootEntity != nullptr && _rootEntity->GetScene() != nullptr)
		{
			_rootEntity->DeleteMe(false);
		}
	}

	void VolumetricTerrain::Initialize(Entity* owner, const SdfTerrainGenerationParams& params)
	{
		// Regenerate safety: tear down old runtime hierarchy first so we don't
		// parent new chunk entities into objects pending deletion.
		for (auto& [coord, chunk] : _chunks)
		{
			if (chunk != nullptr)
			{
				// Root deletion will remove child entities; avoid per-chunk DeleteMe storms.
				chunk->ReleaseEntityReferences(false);
			}
		}
		_chunks.clear();

		if (_rootEntity != nullptr && _rootEntity->GetScene() != nullptr)
		{
			_rootEntity->DeleteMe(false);
		}
		_rootEntity = nullptr;
		_pendingCollisionRefresh = false;
		_collisionRefreshStarted = false;
		_collisionDebounce = 0.0f;
		_collisionStepAccumulator = 0.0f;
		_pendingPvsRefresh = false;
		_initialized = false;
		_gpuVisualsEnabled = false;

		_owner = owner;
		_params = params;
		_params.collisionResolution = std::clamp(_params.collisionResolution, 4, std::max(4, _params.chunkResolution));
		_generator = std::make_unique<SdfTerrainGenerator>(_params);

		if (_owner != nullptr && _owner->GetScene() != nullptr)
		{
			if (_rootEntity == nullptr)
			{
				_rootEntity = _owner->GetScene()->CreateEntity(std::format("{}_Chunks", _owner->GetName()), _owner->GetPosition());
				if (_rootEntity != nullptr)
				{
					_rootEntity->SetParent(_owner);
					_rootEntity->SetFlag(EntityFlags::ExcludeFromHLOD);
				}
			}
		}

		BuildChunks();
		StitchChunkBorders();
		RebuildAll(true);
		if (_owner != nullptr && _owner->GetScene() != nullptr)
		{
			bool canUseGpuVisuals = !_chunks.empty();
			for (auto& [coord, chunk] : _chunks)
			{
				(void)coord;
				if (chunk == nullptr || !chunk->EnsureGpuSurfacePipeline() || !chunk->BuildGpuSurface())
				{
					canUseGpuVisuals = false;
					break;
				}
			}

			_owner->GetScene()->RegisterCustomRenderer(this);
			_gpuVisualsEnabled = canUseGpuVisuals;
			for (auto& [coord, chunk] : _chunks)
			{
				(void)coord;
				if (chunk != nullptr)
				{
					chunk->SetVisualMeshHidden(_gpuVisualsEnabled);
				}
			}
		}
		_initialized = true;
	}

	void VolumetricTerrain::BuildChunks()
	{
		_chunks.clear();
		if (_rootEntity == nullptr)
			return;

		const float halfX = (_params.chunksX * _params.chunkWorldSize) * 0.5f;
		const float halfZ = (_params.chunksZ * _params.chunkWorldSize) * 0.5f;

		for (int32_t z = 0; z < _params.chunksZ; ++z)
		{
			for (int32_t y = 0; y < _params.chunksY; ++y)
			{
				for (int32_t x = 0; x < _params.chunksX; ++x)
				{
					ChunkCoord coord{ x, y, z };
					const math::Vector3 origin(
						_owner->GetPosition().x - halfX + (x * _params.chunkWorldSize),
						_owner->GetPosition().y + (y * _params.chunkWorldSize),
						_owner->GetPosition().z - halfZ + (z * _params.chunkWorldSize));

					auto chunk = std::make_unique<VolumetricTerrainChunk>(coord, _params, origin, _rootEntity);
					chunk->Generate(*_generator);
					_chunks.emplace(coord, std::move(chunk));
				}
			}
		}
	}

	void VolumetricTerrain::RebuildAll(bool rebuildCollision)
	{
		auto begin = std::chrono::high_resolution_clock::now();
		for (auto& [coord, chunk] : _chunks)
		{
			if (chunk == nullptr)
				continue;
			chunk->RebuildMesh(_marchingCubes, rebuildCollision);
		}
		auto end = std::chrono::high_resolution_clock::now();
		_stats.lastMeshRebuildMs = std::chrono::duration<float, std::milli>(end - begin).count();
	}

	void VolumetricTerrain::StitchChunkBorders()
	{
		// Border handling: force identical density/material values on shared chunk faces
		// so marching on each chunk produces matching boundary geometry.
		for (const auto& [coord, chunk] : _chunks)
		{
			if (chunk == nullptr)
			{
				continue;
			}
			StitchChunkBordersAt(coord);
		}
	}

	void VolumetricTerrain::StitchChunkBordersAt(const ChunkCoord& coord)
	{
		auto itSelf = _chunks.find(coord);
		if (itSelf == _chunks.end() || itSelf->second == nullptr)
		{
			return;
		}

		VolumetricTerrainChunk* chunk = itSelf->second.get();
		const int32_t r = chunk->GetResolution();

		auto stitchFace = [&](const ChunkCoord& neighbourCoord, int32_t axis)
		{
			auto it = _chunks.find(neighbourCoord);
			if (it == _chunks.end() || it->second == nullptr)
			{
				return;
			}

			VolumetricTerrainChunk* a = chunk;
			VolumetricTerrainChunk* b = it->second.get();

			for (int32_t i = 0; i <= r; ++i)
			{
				for (int32_t j = 0; j <= r; ++j)
				{
					int32_t ax = 0, ay = 0, az = 0;
					int32_t bx = 0, by = 0, bz = 0;

					if (axis == 0) // +X neighbour
					{
						ax = r; ay = i; az = j;
						bx = 0; by = i; bz = j;
					}
					else if (axis == 1) // +Y neighbour
					{
						ax = i; ay = r; az = j;
						bx = i; by = 0; bz = j;
					}
					else // +Z neighbour
					{
						ax = i; ay = j; az = r;
						bx = i; by = j; bz = 0;
					}

					const float da = a->GetDensityAt(ax, ay, az);
					const float db = b->GetDensityAt(bx, by, bz);
					const float stitched = (da + db) * 0.5f;

					a->SetDensityAt(ax, ay, az, stitched);
					b->SetDensityAt(bx, by, bz, stitched);

					const uint8_t ma = a->GetMaterialAt(ax, ay, az);
					const uint8_t mb = b->GetMaterialAt(bx, by, bz);
					const uint8_t blendedMaterial = (ma == mb) ? ma : ((da <= db) ? ma : mb);
					a->SetMaterialAt(ax, ay, az, blendedMaterial);
					b->SetMaterialAt(bx, by, bz, blendedMaterial);
				}
			}

			a->MarkDirtyMeshOnly();
			b->MarkDirtyMeshOnly();
		};

		stitchFace(ChunkCoord{ coord.x + 1, coord.y, coord.z }, 0);
		stitchFace(ChunkCoord{ coord.x, coord.y + 1, coord.z }, 1);
		stitchFace(ChunkCoord{ coord.x, coord.y, coord.z + 1 }, 2);
	}

	void VolumetricTerrain::RegenerateDirtyChunks()
	{
		auto begin = std::chrono::high_resolution_clock::now();
		_stats.dirtyChunks = 0;
		_stats.totalChunks = static_cast<int32_t>(_chunks.size());
		_stats.triangleCount = 0;
		_stats.densityMemoryBytes = 0;
		int32_t rebuiltThisTick = 0;
		const int32_t meshRebuildBudget = _gpuVisualsEnabled ? (_sculptingActive ? 48 : 24) : (_pendingCollisionRefresh ? 2 : 8);

		for (auto& [coord, chunk] : _chunks)
		{
			if (chunk == nullptr)
				continue;

			if (!_gpuVisualsEnabled && chunk->IsMeshDirty())
			{
				++_stats.dirtyChunks;
				if (rebuiltThisTick < meshRebuildBudget)
				{
					chunk->RebuildMesh(_marchingCubes, false);
					++rebuiltThisTick;
				}
			}
			else if (_gpuVisualsEnabled && chunk->IsMeshDirty())
			{
				++_stats.dirtyChunks;
				if (rebuiltThisTick < meshRebuildBudget)
				{
					if (!chunk->BuildGpuSurface())
					{
						_gpuVisualsEnabled = false;
						for (auto& [fallbackCoord, fallbackChunk] : _chunks)
						{
							(void)fallbackCoord;
							if (fallbackChunk != nullptr)
							{
								fallbackChunk->SetVisualMeshHidden(false);
							}
						}
					}
					++rebuiltThisTick;
				}
			}
			else if (!_gpuVisualsEnabled && chunk->IsCollisionDirty() && !_pendingCollisionRefresh)
			{
				++_stats.dirtyChunks;
				chunk->RebuildCollision();
			}
			else if (chunk->IsMeshDirty() || chunk->IsCollisionDirty())
			{
				++_stats.dirtyChunks;
			}

			if (auto* entity = chunk->GetEntity(); entity != nullptr)
			{
				if (auto* smc = entity->GetComponent<StaticMeshComponent>(); smc != nullptr)
				{
					if (auto mesh = smc->GetMesh(); mesh != nullptr)
					{
						_stats.triangleCount += mesh->GetNumFaces();
					}
				}
			}

			_stats.densityMemoryBytes += static_cast<uint64_t>(chunk->GetDensities().size() * sizeof(float));
		}

		auto end = std::chrono::high_resolution_clock::now();
		_stats.lastMeshRebuildMs = std::chrono::duration<float, std::milli>(end - begin).count();
	}

	void VolumetricTerrain::Tick(float deltaTime)
	{
		if (!_initialized)
			return;

		RegenerateDirtyChunks();

		if (_pendingCollisionRefresh)
		{
			_collisionDebounce += deltaTime;
			if (!_sculptingActive && _collisionDebounce > 2.0f)
			{
				_collisionStepAccumulator += deltaTime;
				if (_collisionStepAccumulator >= 0.05f)
				{
					_collisionStepAccumulator = 0.0f;
					if (ProcessCollisionRefreshStep(1))
					{
						_pendingCollisionRefresh = false;
						_collisionRefreshStarted = false;
						_collisionDebounce = 0.0f;

						if (_pendingPvsRefresh)
						{
							if (auto scene = GetTerrainScene(_owner, _rootEntity); scene != nullptr)
							{
								scene->ForceRebuildPVS();
							}
							_pendingPvsRefresh = false;
						}
					}
				}
			}
		}
	}

	void VolumetricTerrain::MarkNeighbourChunksDirty(const ChunkCoord& coord, const math::Vector3& worldPosition, float brushRadius)
	{
		auto itSelf = _chunks.find(coord);
		if (itSelf == _chunks.end() || itSelf->second == nullptr)
		{
			return;
		}

		const math::Vector3 origin = itSelf->second->GetOrigin();
		const float size = _params.chunkWorldSize;
		const math::Vector3 local = worldPosition - origin;
		const float eps = std::max(0.001f, brushRadius);

		const bool touchNegX = (local.x - eps) <= 0.0f;
		const bool touchPosX = (local.x + eps) >= size;
		const bool touchNegY = (local.y - eps) <= 0.0f;
		const bool touchPosY = (local.y + eps) >= size;
		const bool touchNegZ = (local.z - eps) <= 0.0f;
		const bool touchPosZ = (local.z + eps) >= size;

		auto markIfExists = [&](int32_t dx, int32_t dy, int32_t dz)
		{
			ChunkCoord neighbour{ coord.x + dx, coord.y + dy, coord.z + dz };
			if (auto it = _chunks.find(neighbour); it != _chunks.end() && it->second != nullptr)
			{
				it->second->MarkDirtyMeshOnly();
			}
		};

		if (touchNegX) markIfExists(-1, 0, 0);
		if (touchPosX) markIfExists(1, 0, 0);
		if (touchNegY) markIfExists(0, -1, 0);
		if (touchPosY) markIfExists(0, 1, 0);
		if (touchNegZ) markIfExists(0, 0, -1);
		if (touchPosZ) markIfExists(0, 0, 1);
	}

	bool VolumetricTerrain::ApplyBrush(const math::Vector3& worldPosition, const BrushSettings& settings, float deltaTime)
	{
		if (!_initialized)
			return false;

		bool modifiedAny = false;
		std::vector<ChunkCoord> modifiedChunks;
		std::unordered_set<ChunkCoord, ChunkCoordHash> chunksToRebuild;
		static constexpr ChunkCoord kFaceNeighbours[6] =
		{
			{ 1, 0, 0 }, { -1, 0, 0 },
			{ 0, 1, 0 }, { 0, -1, 0 },
			{ 0, 0, 1 }, { 0, 0, -1 },
		};
		for (auto& [coord, chunk] : _chunks)
		{
			if (chunk == nullptr)
				continue;

			const math::Vector3 chunkMin = chunk->GetOrigin();
			const math::Vector3 chunkMax = chunkMin + math::Vector3(_params.chunkWorldSize, _params.chunkWorldSize, _params.chunkWorldSize);
			const math::Vector3 closestPoint(
				std::clamp(worldPosition.x, chunkMin.x, chunkMax.x),
				std::clamp(worldPosition.y, chunkMin.y, chunkMax.y),
				std::clamp(worldPosition.z, chunkMin.z, chunkMax.z));
			const math::Vector3 toBrush = worldPosition - closestPoint;
			if (toBrush.LengthSquared() > (settings.radius * settings.radius))
			{
				continue;
			}

			if (chunk->ApplyBrush(worldPosition, settings, deltaTime))
			{
				modifiedAny = true;
				modifiedChunks.push_back(coord);
				chunksToRebuild.insert(coord);
				MarkNeighbourChunksDirty(coord, worldPosition, settings.radius);
			}
		}

		if (modifiedAny)
		{
			const bool deferBorderStitch = _gpuVisualsEnabled && _sculptingActive;
			for (const ChunkCoord& coord : modifiedChunks)
			{
				if (!deferBorderStitch)
				{
					StitchChunkBordersAt(coord);
					StitchChunkBordersAt(ChunkCoord{ coord.x - 1, coord.y, coord.z });
					StitchChunkBordersAt(ChunkCoord{ coord.x, coord.y - 1, coord.z });
					StitchChunkBordersAt(ChunkCoord{ coord.x, coord.y, coord.z - 1 });
				}

				for (const ChunkCoord& neighbourOffset : kFaceNeighbours)
				{
					chunksToRebuild.insert(ChunkCoord{ coord.x + neighbourOffset.x, coord.y + neighbourOffset.y, coord.z + neighbourOffset.z });
				}
			}

			if (_gpuVisualsEnabled)
			{
				for (const ChunkCoord& rebuildCoord : chunksToRebuild)
				{
					auto it = _chunks.find(rebuildCoord);
					if (it != _chunks.end() && it->second != nullptr)
					{
						it->second->SyncDensityToGpu();
					}
				}
			}
			QueueCollisionRefresh();
			_pendingPvsRefresh = true;
		}

		return modifiedAny;
	}

	void VolumetricTerrain::QueueCollisionRefresh()
	{
		_pendingCollisionRefresh = true;
		_collisionRefreshStarted = false;
		_collisionDebounce = 0.0f;
		_collisionStepAccumulator = 0.0f;
	}

	bool VolumetricTerrain::ProcessCollisionRefreshStep(int32_t chunkBudget)
	{
		if (!_pendingCollisionRefresh)
		{
			return true;
		}

		if (!_collisionRefreshStarted)
		{
			StitchChunkBorders();
			_collisionRefreshStarted = true;
		}

		int32_t processed = 0;
		bool anyRemaining = false;
		for (auto& [coord, chunk] : _chunks)
		{
			(void)coord;
			if (chunk == nullptr)
			{
				continue;
			}

			if (!(chunk->IsCollisionDirty() || chunk->IsMeshDirty()))
			{
				continue;
			}
			if (processed >= chunkBudget)
			{
				anyRemaining = true;
				continue;
			}

			chunk->RebuildMesh(_marchingCubes, true);
			if (_gpuVisualsEnabled)
			{
				chunk->SetVisualMeshHidden(true);
			}
			++processed;
		}

		return !anyRemaining;
	}

	void VolumetricTerrain::FlushCollisionRefresh()
	{
		if (!_pendingCollisionRefresh)
		{
			return;
		}
		while (!ProcessCollisionRefreshStep(std::numeric_limits<int32_t>::max()))
		{
		}

		_pendingCollisionRefresh = false;
		_collisionRefreshStarted = false;
		_collisionDebounce = 0.0f;
		_collisionStepAccumulator = 0.0f;

		if (_pendingPvsRefresh)
		{
			if (auto scene = GetTerrainScene(_owner, _rootEntity); scene != nullptr)
			{
				scene->ForceRebuildPVS();
			}
			_pendingPvsRefresh = false;
		}
	}

	void VolumetricTerrain::GatherEditedChunkData(std::vector<EditedChunkBlob>& chunks) const
	{
		chunks.clear();
		for (const auto& [coord, chunk] : _chunks)
		{
			if (chunk == nullptr || !chunk->HasEdits())
				continue;

			EditedChunkBlob blob;
			blob.coord = coord;
			blob.densities = chunk->GetDensities();
			blob.materials = chunk->GetMaterials();
			chunks.push_back(std::move(blob));
		}
	}

	void VolumetricTerrain::ApplyEditedChunkData(const std::vector<EditedChunkBlob>& chunks)
	{
		for (const auto& blob : chunks)
		{
			if (auto it = _chunks.find(blob.coord); it != _chunks.end() && it->second != nullptr)
			{
				it->second->SetEditedData(blob.densities, blob.materials);
				MarkNeighbourChunksDirty(blob.coord, it->second->GetBounds().Center, 0.0f);
			}
		}

		StitchChunkBorders();
	}

	void VolumetricTerrain::Serialize(json& data, JsonFile* file)
	{
		json& terrain = data["volumetricTerrain"];
		terrain["version"] = 1;
		_params.Serialize(terrain["generation"], file);

		std::vector<EditedChunkBlob> edited;
		GatherEditedChunkData(edited);

		// Save format:
		// - generation params are persisted once
		// - only edited chunks serialize density/material blobs
		// This avoids storing the full generated world volume on disk.
		json editedChunks = json::array();
		for (const auto& chunk : edited)
		{
			json out;
			out["coord"] = { chunk.coord.x, chunk.coord.y, chunk.coord.z };
			out["densities"] = chunk.densities;
			out["materials"] = chunk.materials;
			editedChunks.push_back(std::move(out));
		}

		terrain["editedChunks"] = std::move(editedChunks);
	}

	void VolumetricTerrain::Deserialize(json& data, JsonFile* file)
	{
		auto it = data.find("volumetricTerrain");
		if (it == data.end())
			return;

		json& terrain = *it;
		SdfTerrainGenerationParams loadedParams = _params;
		if (terrain.contains("generation"))
		{
			loadedParams.Deserialize(terrain["generation"], file);
		}

		Initialize(_owner, loadedParams);

		std::vector<EditedChunkBlob> edited;
		if (terrain.contains("editedChunks") && terrain["editedChunks"].is_array())
		{
			for (const auto& item : terrain["editedChunks"])
			{
				if (!item.is_object() || !item.contains("coord") || !item["coord"].is_array())
					continue;

				EditedChunkBlob chunk;
				chunk.coord.x = item["coord"][0].get<int32_t>();
				chunk.coord.y = item["coord"][1].get<int32_t>();
				chunk.coord.z = item["coord"][2].get<int32_t>();
				if (item.contains("densities")) chunk.densities = item["densities"].get<std::vector<float>>();
				if (item.contains("materials")) chunk.materials = item["materials"].get<std::vector<uint8_t>>();
				edited.push_back(std::move(chunk));
			}
		}

		ApplyEditedChunkData(edited);
		RebuildAll(true);
	}

	Entity* VolumetricTerrain::FindChunkEntityFromRayHit(Entity* hitEntity) const
	{
		for (Entity* e = hitEntity; e != nullptr; e = e->GetParent())
		{
			if (e == _rootEntity)
				return hitEntity;
		}
		return nullptr;
	}

	bool VolumetricTerrain::RaycastTerrainBounds(const math::Ray& ray, math::Vector3& hitPosition) const
	{
		if (_owner == nullptr)
		{
			return false;
		}

		const float sizeX = static_cast<float>(_params.chunksX) * _params.chunkWorldSize;
		const float sizeY = static_cast<float>(_params.chunksY) * _params.chunkWorldSize;
		const float sizeZ = static_cast<float>(_params.chunksZ) * _params.chunkWorldSize;
		const math::Vector3 extents(sizeX * 0.5f, sizeY * 0.5f, sizeZ * 0.5f);
		const math::Vector3 center(
			_owner->GetPosition().x,
			_owner->GetPosition().y + extents.y,
			_owner->GetPosition().z);
		const math::Vector3 bmin = center - extents;
		const math::Vector3 bmax = center + extents;
		const math::Vector3 dir = ray.direction;
		const math::Vector3 org = ray.position;

		constexpr float kEpsilon = 1e-6f;
		float tMin = -FLT_MAX;
		float tMax = FLT_MAX;

		auto testAxis = [&](float origin, float direction, float minv, float maxv) -> bool
		{
			if (fabsf(direction) < kEpsilon)
			{
				return origin >= minv && origin <= maxv;
			}

			float t1 = (minv - origin) / direction;
			float t2 = (maxv - origin) / direction;
			if (t1 > t2)
			{
				std::swap(t1, t2);
			}

			tMin = std::max(tMin, t1);
			tMax = std::min(tMax, t2);
			return tMin <= tMax;
		};

		if (!testAxis(org.x, dir.x, bmin.x, bmax.x) ||
			!testAxis(org.y, dir.y, bmin.y, bmax.y) ||
			!testAxis(org.z, dir.z, bmin.z, bmax.z))
		{
			return false;
		}

		// If ray starts inside the terrain AABB, use exit distance (tMax).
		// Otherwise use entry distance (tMin).
		float tHit = (tMin >= 0.0f) ? tMin : tMax;
		if (tHit < 0.0f)
		{
			return false;
		}

		hitPosition = org + (dir * tHit);
		return true;
	}

	bool VolumetricTerrain::RaycastTerrainSurface(const math::Ray& ray, float maxDistance, math::Vector3& hitPosition) const
	{
		if (_owner == nullptr || _chunks.empty())
		{
			return false;
		}

		const float sizeX = static_cast<float>(_params.chunksX) * _params.chunkWorldSize;
		const float sizeY = static_cast<float>(_params.chunksY) * _params.chunkWorldSize;
		const float sizeZ = static_cast<float>(_params.chunksZ) * _params.chunkWorldSize;
		const math::Vector3 extents(sizeX * 0.5f, sizeY * 0.5f, sizeZ * 0.5f);
		const math::Vector3 center(
			_owner->GetPosition().x,
			_owner->GetPosition().y + extents.y,
			_owner->GetPosition().z);
		const math::Vector3 bmin = center - extents;
		const math::Vector3 bmax = center + extents;
		const math::Vector3 dir = ray.direction;
		const math::Vector3 org = ray.position;

		constexpr float kEpsilon = 1e-6f;
		float tMin = -FLT_MAX;
		float tMax = FLT_MAX;

		auto testAxis = [&](float origin, float direction, float minv, float maxv) -> bool
		{
			if (fabsf(direction) < kEpsilon)
			{
				return origin >= minv && origin <= maxv;
			}

			float t1 = (minv - origin) / direction;
			float t2 = (maxv - origin) / direction;
			if (t1 > t2)
			{
				std::swap(t1, t2);
			}

			tMin = std::max(tMin, t1);
			tMax = std::min(tMax, t2);
			return tMin <= tMax;
		};

		if (!testAxis(org.x, dir.x, bmin.x, bmax.x) ||
			!testAxis(org.y, dir.y, bmin.y, bmax.y) ||
			!testAxis(org.z, dir.z, bmin.z, bmax.z))
		{
			return false;
		}

		const float rayStart = std::max(0.0f, tMin);
		const float rayEnd = std::min(std::max(0.0f, tMax), std::max(0.0f, maxDistance));
		if (rayEnd <= rayStart)
		{
			return false;
		}

		const math::Vector3 worldMin(
			_owner->GetPosition().x - (sizeX * 0.5f),
			_owner->GetPosition().y,
			_owner->GetPosition().z - (sizeZ * 0.5f));

		const float step = std::max(0.2f, _params.chunkWorldSize / static_cast<float>(std::max(1, _params.chunkResolution)) * 0.5f);

		auto sampleDensity = [&](const math::Vector3& worldPos, float& outDensity) -> bool
		{
			const math::Vector3 local = worldPos - worldMin;
			const int32_t cx = static_cast<int32_t>(floorf(local.x / _params.chunkWorldSize));
			const int32_t cy = static_cast<int32_t>(floorf(local.y / _params.chunkWorldSize));
			const int32_t cz = static_cast<int32_t>(floorf(local.z / _params.chunkWorldSize));

			if (cx < 0 || cy < 0 || cz < 0 || cx >= _params.chunksX || cy >= _params.chunksY || cz >= _params.chunksZ)
			{
				return false;
			}

			const ChunkCoord coord{ cx, cy, cz };
			auto it = _chunks.find(coord);
			if (it == _chunks.end() || it->second == nullptr || !it->second->IsGenerated())
			{
				return false;
			}

			outDensity = it->second->SampleDensityNearestWorld(worldPos);
			return true;
		};

		float prevT = rayStart;
		float prevD = 1.0f;
		bool havePrev = false;

		for (float t = rayStart; t <= rayEnd; t += step)
		{
			const math::Vector3 p = org + (dir * t);
			float d = 1.0f;
			if (!sampleDensity(p, d))
			{
				continue;
			}

			if (!havePrev)
			{
				prevT = t;
				prevD = d;
				havePrev = true;
				continue;
			}

			// Density convention: < 0 solid, >= 0 empty. Find first zero crossing.
			if ((prevD >= 0.0f && d < 0.0f) || (prevD < 0.0f && d >= 0.0f))
			{
				const float denom = (prevD - d);
				const float alpha = (fabsf(denom) > kEpsilon) ? std::clamp(prevD / denom, 0.0f, 1.0f) : 0.5f;
				const float hitT = std::lerp(prevT, t, alpha);
				hitPosition = org + (dir * hitT);
				return true;
			}

			prevT = t;
			prevD = d;
		}

		return false;
	}

	void VolumetricTerrain::RenderCustom(Scene* scene, Camera* camera, MeshRenderFlags renderFlags)
	{
		(void)scene;
		(void)camera;
		if (!_gpuVisualsEnabled || (renderFlags & MeshRenderFlags::MeshRenderNormal) == 0 || (renderFlags & MeshRenderFlags::MeshRenderTransparency) != 0 || (renderFlags & MeshRenderFlags::MeshRenderShadowMap) != 0)
		{
			return;
		}

		for (auto& [coord, chunk] : _chunks)
		{
			(void)coord;
			if (chunk != nullptr)
			{
				chunk->RenderGpuSurface();
			}
		}
	}
}
