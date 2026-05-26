#include "VolumetricTerrain.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <limits>
#include <string>
#include <thread>
#include <unordered_set>

namespace HexEngine::VolumetricTerrain
{
	namespace
	{
		// Bumped to 2 when cookedCollisionBlob was added at the tail of the
		// payload. v1 payloads still load (the blob is left empty), v2
		// payloads gain a "cooked PhysX bytes" tail.
		static constexpr uint32_t kEditedChunkPayloadVersion = 2u;

		struct QuantizedDensityBlob
		{
			float minValue = 0.0f;
			float maxValue = 0.0f;
			std::vector<uint16_t> values;
		};

		void WriteBytes(std::vector<uint8_t>& buffer, const void* data, size_t size)
		{
			const auto* bytes = reinterpret_cast<const uint8_t*>(data);
			buffer.insert(buffer.end(), bytes, bytes + size);
		}

		template <typename T>
		void WriteValue(std::vector<uint8_t>& buffer, const T& value)
		{
			WriteBytes(buffer, &value, sizeof(T));
		}

		template <typename T>
		bool ReadValue(const std::vector<uint8_t>& buffer, size_t& offset, T& value)
		{
			if ((offset + sizeof(T)) > buffer.size())
			{
				return false;
			}

			memcpy(&value, buffer.data() + offset, sizeof(T));
			offset += sizeof(T);
			return true;
		}

		std::string Base64Encode(const std::vector<uint8_t>& data)
		{
			static constexpr char kBase64Table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
			std::string encoded;
			encoded.reserve(((data.size() + 2ull) / 3ull) * 4ull);

			for (size_t i = 0; i < data.size(); i += 3)
			{
				const uint32_t b0 = data[i];
				const uint32_t b1 = (i + 1 < data.size()) ? data[i + 1] : 0u;
				const uint32_t b2 = (i + 2 < data.size()) ? data[i + 2] : 0u;
				const uint32_t triple = (b0 << 16) | (b1 << 8) | b2;

				encoded.push_back(kBase64Table[(triple >> 18) & 0x3Fu]);
				encoded.push_back(kBase64Table[(triple >> 12) & 0x3Fu]);
				encoded.push_back((i + 1 < data.size()) ? kBase64Table[(triple >> 6) & 0x3Fu] : '=');
				encoded.push_back((i + 2 < data.size()) ? kBase64Table[triple & 0x3Fu] : '=');
			}

			return encoded;
		}

		bool Base64Decode(const std::string& encoded, std::vector<uint8_t>& output)
		{
			static constexpr int8_t kDecodeTable[256] =
			{
				-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
				-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-2,-1,-1,
				-1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
				-1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
				-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
				-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
				-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
				-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
			};

			output.clear();
			if (encoded.empty())
			{
				return true;
			}

			int32_t val = 0;
			int32_t bits = -8;
			for (const unsigned char c : encoded)
			{
				const int8_t decoded = kDecodeTable[c];
				if (decoded == -1)
				{
					continue;
				}
				if (decoded == -2)
				{
					break;
				}

				val = (val << 6) + decoded;
				bits += 6;
				if (bits >= 0)
				{
					output.push_back(static_cast<uint8_t>((val >> bits) & 0xFF));
					bits -= 8;
				}
			}

			return true;
		}

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

		QuantizedDensityBlob QuantizeDensities(const std::vector<float>& densities)
		{
			QuantizedDensityBlob blob{};
			if (densities.empty())
			{
				return blob;
			}

			auto [minIt, maxIt] = std::minmax_element(densities.begin(), densities.end());
			blob.minValue = *minIt;
			blob.maxValue = *maxIt;
			blob.values.resize(densities.size(), 0u);

			const float range = blob.maxValue - blob.minValue;
			if (range <= 0.000001f)
			{
				return blob;
			}

			for (size_t i = 0; i < densities.size(); ++i)
			{
				const float normalized = std::clamp((densities[i] - blob.minValue) / range, 0.0f, 1.0f);
				blob.values[i] = static_cast<uint16_t>(std::clamp(normalized * 65535.0f, 0.0f, 65535.0f));
			}

			return blob;
		}

		std::vector<float> DequantizeDensities(const QuantizedDensityBlob& blob)
		{
			std::vector<float> densities(blob.values.size(), blob.minValue);
			const float range = blob.maxValue - blob.minValue;
			if (range <= 0.000001f)
			{
				return densities;
			}

			for (size_t i = 0; i < blob.values.size(); ++i)
			{
				const float normalized = static_cast<float>(blob.values[i]) / 65535.0f;
				densities[i] = blob.minValue + (normalized * range);
			}

			return densities;
		}

		bool SerializeEditedChunkBlobBinary(const EditedChunkBlob& chunk, std::string& encoded)
		{
			std::vector<uint8_t> payload;
			WriteValue(payload, kEditedChunkPayloadVersion);

			const QuantizedDensityBlob packed = QuantizeDensities(chunk.densities);
			const uint32_t densityCount = static_cast<uint32_t>(packed.values.size());
			WriteValue(payload, densityCount);
			WriteValue(payload, packed.minValue);
			WriteValue(payload, packed.maxValue);
			if (!packed.values.empty())
			{
				WriteBytes(payload, packed.values.data(), packed.values.size() * sizeof(uint16_t));
			}

			const uint8_t hasMaterialEdits = chunk.hasMaterialEdits ? 1u : 0u;
			WriteValue(payload, hasMaterialEdits);
			if (chunk.hasMaterialEdits)
			{
				const uint32_t materialWeightCount = static_cast<uint32_t>(chunk.materialWeights.size());
				WriteValue(payload, materialWeightCount);
				if (!chunk.materialWeights.empty())
				{
					WriteBytes(payload, chunk.materialWeights.data(), chunk.materialWeights.size());
				}
			}

			// v2 tail: cooked PhysX collision blob. uint32 count + raw bytes.
			// When the runtime hasn't cooked this chunk yet (fresh terrain
			// before the first collision refresh) the count is 0 and the load
			// path falls back to the normal cook-on-demand flow.
			const uint32_t cookedBlobSize = static_cast<uint32_t>(chunk.cookedCollisionBlob.size());
			WriteValue(payload, cookedBlobSize);
			if (cookedBlobSize > 0)
			{
				WriteBytes(payload, chunk.cookedCollisionBlob.data(), cookedBlobSize);
			}

			std::vector<uint8_t> compressed;
			if (g_pEnv->_compressionProvider != nullptr && g_pEnv->_compressionProvider->CompressData(payload, compressed) && !compressed.empty())
			{
				encoded = Base64Encode(compressed);
				return true;
			}

			encoded = Base64Encode(payload);
			return true;
		}

		bool DeserializeEditedChunkBlobBinary(const std::string& encoded, EditedChunkBlob& chunk)
		{
			std::vector<uint8_t> payload;
			if (!Base64Decode(encoded, payload) || payload.empty())
			{
				return false;
			}

			std::vector<uint8_t> decompressed;
			if (g_pEnv->_compressionProvider != nullptr)
			{
				if (g_pEnv->_compressionProvider->DecompressData(payload, decompressed) && !decompressed.empty())
				{
					payload = std::move(decompressed);
				}
			}

			size_t offset = 0;
			uint32_t version = 0;
			if (!ReadValue(payload, offset, version) || version < 1u || version > kEditedChunkPayloadVersion)
			{
				return false;
			}

			QuantizedDensityBlob packed;
			uint32_t densityCount = 0;
			if (!ReadValue(payload, offset, densityCount) ||
				!ReadValue(payload, offset, packed.minValue) ||
				!ReadValue(payload, offset, packed.maxValue))
			{
				return false;
			}
			packed.values.resize(densityCount);
			if (densityCount > 0)
			{
				const size_t densityBytes = static_cast<size_t>(densityCount) * sizeof(uint16_t);
				if ((offset + densityBytes) > payload.size())
				{
					return false;
				}
				memcpy(packed.values.data(), payload.data() + offset, densityBytes);
				offset += densityBytes;
			}
			chunk.densities = DequantizeDensities(packed);

			uint8_t hasMaterialEdits = 0u;
			if (!ReadValue(payload, offset, hasMaterialEdits))
			{
				return false;
			}
			chunk.hasMaterialEdits = (hasMaterialEdits != 0u);
			if (chunk.hasMaterialEdits)
			{
				uint32_t materialWeightCount = 0;
				if (!ReadValue(payload, offset, materialWeightCount))
				{
					return false;
				}
				chunk.materialWeights.resize(materialWeightCount);
				if (materialWeightCount > 0)
				{
					if ((offset + materialWeightCount) > payload.size())
					{
						return false;
					}
					memcpy(chunk.materialWeights.data(), payload.data() + offset, materialWeightCount);
					offset += materialWeightCount;
				}
			}

			// v2 tail: cached cooked-PhysX collision blob. v1 payloads stop
			// here and leave cookedCollisionBlob empty (forcing a re-cook on
			// load); v2 payloads have a size + bytes block.
			if (version >= 2u)
			{
				uint32_t cookedBlobSize = 0;
				if (ReadValue(payload, offset, cookedBlobSize))
				{
					chunk.cookedCollisionBlob.resize(cookedBlobSize);
					if (cookedBlobSize > 0)
					{
						if ((offset + cookedBlobSize) > payload.size())
						{
							// Partial blob - treat as missing, fall through to cook-on-demand.
							chunk.cookedCollisionBlob.clear();
						}
						else
						{
							memcpy(chunk.cookedCollisionBlob.data(), payload.data() + offset, cookedBlobSize);
							offset += cookedBlobSize;
						}
					}
				}
			}

			return true;
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
			_rootEntity->SetParent(nullptr);
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
			_rootEntity->SetParent(nullptr);
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
		_lastOwnerPosition = _owner != nullptr ? _owner->GetPosition() : math::Vector3::Zero;
		_generator = std::make_unique<SdfTerrainGenerator>(_params);

		if (_owner != nullptr && _owner->GetScene() != nullptr)
		{
			if (_rootEntity == nullptr)
			{
				_rootEntity = _owner->GetScene()->CreateEntity(std::format("{}_Chunks", _owner->GetName()), _owner->GetPosition());
				if (_rootEntity != nullptr)
				{
					_rootEntity->SetParent(_owner);
					_rootEntity->SetFlag(EntityFlags::ExcludeFromHLOD | EntityFlags::DoNotSave);
				}
			}
		}

		// Phase timings for terrain load - 30s+ stutter has been observed and
		// we need to know where the time actually goes before optimising.
		// Logged at INFO so it shows up in normal runs without needing
		// LOG_DEBUG enabled.
		using clk = std::chrono::high_resolution_clock;
		const auto t0 = clk::now();

		BuildChunks();
		const auto t1 = clk::now();

		StitchChunkBorders();
		const auto t2 = clk::now();

		BuildGpuVisualsOrFallback(true);
		const auto t3 = clk::now();

		_initialized = true;

		const int32_t chunkCount = _params.chunksX * _params.chunksY * _params.chunksZ;
		const auto ms = [](auto a, auto b) {
			return std::chrono::duration<double, std::milli>(b - a).count();
		};
		LOG_INFO("VolumetricTerrain::Initialize: %d chunks (%dx%dx%d) @ res %d  total=%.1fms  BuildChunks=%.1fms  StitchBorders=%.1fms  BuildGpuVisualsOrFallback=%.1fms",
			chunkCount,
			_params.chunksX, _params.chunksY, _params.chunksZ,
			_params.chunkResolution,
			ms(t0, t3), ms(t0, t1), ms(t1, t2), ms(t2, t3));
	}

	void VolumetricTerrain::Regenerate(bool preserveEdits)
	{
		std::vector<EditedChunkBlob> editedChunks;
		if (preserveEdits && _initialized)
		{
			if (_pendingCollisionRefresh)
			{
				FlushCollisionRefresh();
			}
			GatherEditedChunkData(editedChunks);
		}

		Initialize(_owner, _params);

		if (!preserveEdits || editedChunks.empty())
		{
			return;
		}

		ApplyEditedChunkData(editedChunks);
		BuildGpuVisualsOrFallback(true);
	}

	void VolumetricTerrain::SetOwner(Entity* owner)
	{
		_owner = owner;
		_lastOwnerPosition = _owner != nullptr ? _owner->GetPosition() : math::Vector3::Zero;
	}

	void VolumetricTerrain::BuildChunks()
	{
		_chunks.clear();
		if (_rootEntity == nullptr)
			return;

		const float halfX = (_params.chunksX * _params.chunkWorldSize) * 0.5f;
		const float halfZ = (_params.chunksZ * _params.chunkWorldSize) * 0.5f;

		using clk = std::chrono::high_resolution_clock;

		// Phase A (serial, on main thread): construct all chunk wrappers,
		// which creates the per-chunk Entity + StaticMeshComponent + RigidBody.
		// These touch the scene graph and Scene::CreateEntity isn't safe to
		// call from worker threads. Cheap (under 2ms per chunk in our
		// measurements) so serial here is fine.
		const auto ctorStart = clk::now();
		std::vector<VolumetricTerrainChunk*> chunkPtrs;
		chunkPtrs.reserve(static_cast<size_t>(_params.chunksX * _params.chunksY * _params.chunksZ));

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
					chunkPtrs.push_back(chunk.get());
					_chunks.emplace(coord, std::move(chunk));
				}
			}
		}
		const auto ctorDone = clk::now();

		// Phase B (parallel, on worker threads): for each chunk, either
		// apply pre-baked snapshot data (if the scene was saved with one)
		// or run SDF generation. ApplyBakedData is a memcpy + small CPU
		// bookkeeping (negligible vs ~100ms of SDF eval per chunk), so
		// the parallel scheduling still benefits even if every chunk
		// has a snapshot - and crucially eliminates the SDF cost entirely.
		std::atomic<size_t> nextIndex{ 0 };
		std::atomic<int32_t> bakedAppliedCount{ 0 };
		std::atomic<int32_t> sdfGeneratedCount{ 0 };
		const unsigned hw = std::thread::hardware_concurrency();
		// Leave one core for the main thread so we don't compete for the
		// scheduler with whatever else the engine is doing this frame.
		const unsigned threadCount = std::max(1u, hw > 1 ? hw - 1 : 1u);

		std::vector<std::thread> workers;
		workers.reserve(threadCount);
		for (unsigned t = 0; t < threadCount; ++t)
		{
			workers.emplace_back([&]()
			{
				while (true)
				{
					const size_t i = nextIndex.fetch_add(1, std::memory_order_relaxed);
					if (i >= chunkPtrs.size())
						return;
					auto* chunk = chunkPtrs[i];
					const auto baked = _bakedChunks.find(chunk->GetCoord());
					if (baked != _bakedChunks.end()
						&& chunk->ApplyBakedData(baked->second.densities, baked->second.materials, baked->second.materialWeights))
					{
						// Snapshot also carries the pre-cooked PhysX collision
						// blob when one was saved - hand it to the chunk so
						// the upcoming collision refresh can take the cache
						// fast path instead of running PxCookTriangleMesh.
						if (!baked->second.cookedCollisionBlob.empty())
							chunk->SetCachedCookedCollisionBlob(baked->second.cookedCollisionBlob);
						bakedAppliedCount.fetch_add(1, std::memory_order_relaxed);
					}
					else
					{
						chunk->GenerateCpu(*_generator);
						sdfGeneratedCount.fetch_add(1, std::memory_order_relaxed);
					}
				}
			});
		}
		for (auto& w : workers)
			w.join();
		const auto cpuGenDone = clk::now();

		// Phase C (serial, on main thread): per-chunk GPU upload. The D3D11
		// immediate context isn't safe to drive from multiple threads so we
		// can't trivially parallelise this; in practice GPU upload is a small
		// fraction of the SDF eval cost so leaving it serial is fine.
		for (auto* chunk : chunkPtrs)
			chunk->UploadGeneratedToGpu();
		const auto uploadDone = clk::now();

		const auto ms = [](auto a, auto b) {
			return std::chrono::duration<double, std::milli>(b - a).count();
		};
		const double ctorMs = ms(ctorStart, ctorDone);
		const double parallelGenMs = ms(ctorDone, cpuGenDone);
		const double uploadMs = ms(cpuGenDone, uploadDone);
		LOG_INFO("VolumetricTerrain::BuildChunks (parallel, %u workers): ctor+entity=%.1fms  parallel-Generate=%.1fms (baked=%d, sdf=%d)  GPU-upload=%.1fms",
			threadCount, ctorMs, parallelGenMs,
			bakedAppliedCount.load(), sdfGeneratedCount.load(),
			uploadMs);

		// Snapshot data has now been consumed (or fallen through to SDF
		// generation per chunk). Free the memory - keeping it around would
		// double the working set, and any future Regenerate would either
		// run with fresh SDF or be re-fed by a fresh Deserialize.
		_bakedChunks.clear();
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

	void VolumetricTerrain::BuildGpuVisualsOrFallback(bool rebuildCollisionFallback)
	{
		using clk = std::chrono::high_resolution_clock;
		const auto pathStart = clk::now();

		if (_owner == nullptr || _owner->GetScene() == nullptr)
		{
			_gpuVisualsEnabled = false;
			RebuildAll(rebuildCollisionFallback);
			LOG_INFO("VolumetricTerrain::BuildGpuVisualsOrFallback: no scene -> RebuildAll fallback took %.1fms",
				std::chrono::duration<double, std::milli>(clk::now() - pathStart).count());
			return;
		}

		bool canUseGpuVisuals = !_chunks.empty();
		for (auto& [coord, chunk] : _chunks)
		{
			(void)coord;
			if (chunk == nullptr || !chunk->EnsureGpuSurfacePipeline())
			{
				canUseGpuVisuals = false;
				break;
			}

			for (uint32_t lodIndex = 0; lodIndex < VolumetricTerrainChunk::kGpuSurfaceLodCount; ++lodIndex)
			{
				if (!chunk->BuildGpuSurface(lodIndex))
				{
					canUseGpuVisuals = false;
					break;
				}
			}

			if (!canUseGpuVisuals)
			{
				break;
			}
		}

		const auto gpuPipelineDoneAt = clk::now();

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

		if (_gpuVisualsEnabled)
		{
			_pendingCollisionRefresh = true;
			_collisionRefreshStarted = false;
			_collisionDebounce = 0.0f;
			_collisionStepAccumulator = 0.0f;
			LOG_INFO("VolumetricTerrain::BuildGpuVisualsOrFallback: GPU path enabled  pipeline+surfaces=%.1fms  (collision queued for lazy refresh)",
				std::chrono::duration<double, std::milli>(gpuPipelineDoneAt - pathStart).count());
			return;
		}

		const auto fallbackStart = clk::now();
		RebuildAll(rebuildCollisionFallback);
		LOG_INFO("VolumetricTerrain::BuildGpuVisualsOrFallback: CPU fallback  gpu-attempt=%.1fms  RebuildAll=%.1fms (this includes per-chunk marching cubes + PhysX cook)",
			std::chrono::duration<double, std::milli>(gpuPipelineDoneAt - pathStart).count(),
			std::chrono::duration<double, std::milli>(clk::now() - fallbackStart).count());
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

					const auto wa = a->GetMaterialWeightsAt(ax, ay, az);
					const auto wb = b->GetMaterialWeightsAt(bx, by, bz);
					std::array<uint8_t, 4> stitchedWeights{};
					for (size_t layer = 0; layer < 4; ++layer)
					{
						stitchedWeights[layer] = static_cast<uint8_t>((static_cast<uint16_t>(wa[layer]) + static_cast<uint16_t>(wb[layer])) / 2u);
					}
					a->SetMaterialWeightsAt(ax, ay, az, stitchedWeights);
					b->SetMaterialWeightsAt(bx, by, bz, stitchedWeights);
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
					bool builtAllLods = true;
					for (uint32_t lodIndex = 0; lodIndex < VolumetricTerrainChunk::kGpuSurfaceLodCount; ++lodIndex)
					{
						if (!chunk->BuildGpuSurface(lodIndex))
						{
							builtAllLods = false;
							break;
						}
					}

					if (!builtAllLods)
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

		if (_owner != nullptr)
		{
			const math::Vector3 ownerPosition = _owner->GetPosition();
			const math::Vector3 ownerDelta = ownerPosition - _lastOwnerPosition;
			if (ownerDelta.LengthSquared() > 0.0f)
			{
				for (auto& [coord, chunk] : _chunks)
				{
					(void)coord;
					if (chunk != nullptr)
					{
						chunk->OffsetWorld(ownerDelta);
					}
				}

				_lastOwnerPosition = ownerPosition;
			}
		}

		RegenerateDirtyChunks();

		if (_pendingCollisionRefresh)
		{
			_collisionDebounce += deltaTime;
			// Debounce was 2s to wait out user sculpting before re-cooking.
			// With the async cook pipeline, cooks happen on worker threads
			// and don't block the main thread; the cost of starting one
			// extra cook that gets superseded by a still-newer edit is
			// small, so a much shorter debounce is fine. For non-sculpting
			// (initial load) we want to kick cooks off immediately so the
			// terrain has collision available as quickly as possible.
			const float debounceTarget = _sculptingActive ? 0.5f : 0.05f;
			if (_collisionDebounce > debounceTarget)
			{
				_collisionStepAccumulator += deltaTime;
				if (_collisionStepAccumulator >= 0.05f)
				{
					_collisionStepAccumulator = 0.0f;
					// Budget = hardware_concurrency() so all available cores
					// can be cooking at once. The cooks run on worker threads,
					// so the main-thread cost per tick is just the kickoff
					// (~0.1ms each) + any finalise calls for cooks that have
					// completed since last tick (~5-15ms each, polled).
					const int32_t cookBudget = std::max(1, static_cast<int32_t>(std::thread::hardware_concurrency()));
					if (ProcessCollisionRefreshStep(cookBudget))
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
			const bool forceImmediateBorderStitch =
				settings.mode == BrushMode::Smooth ||
				settings.mode == BrushMode::Flatten;
			const bool deferBorderStitch = _gpuVisualsEnabled && _sculptingActive && !forceImmediateBorderStitch;
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
						it->second->SyncMaterialsToGpu();
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

		using clk = std::chrono::high_resolution_clock;
		const auto stepStart = clk::now();

		if (!_collisionRefreshStarted)
		{
			StitchChunkBorders();
			_collisionRefreshStarted = true;
		}

		// Two-phase: (a) finalise any async cooks that finished since the last
		// tick - these are cheap (~5-15ms each, mostly createTriangleMesh +
		// createShape + attach), and we want them committed promptly so the
		// terrain has collision available; (b) for chunks that still need
		// collision and have no cook in flight, kick off a new async cook.
		// Step a) doesn't count against chunkBudget because the main-thread
		// cost is bounded. Step b) does count because each kicks off another
		// worker.
		int32_t kickedOff = 0;
		int32_t finalised = 0;
		int32_t cacheHits = 0;
		bool anyRemaining = false;

		for (auto& [coord, chunk] : _chunks)
		{
			(void)coord;
			if (chunk == nullptr)
				continue;

			// (a) finalise completed cooks first.
			if (chunk->HasAsyncCollisionInFlight())
			{
				if (chunk->PollAsyncCollisionFinish())
					++finalised;
				else
					anyRemaining = true;  // still cooking
				continue;
			}

			if (!(chunk->IsCollisionDirty() || chunk->IsMeshDirty()))
				continue;

			// (b) cached cook fast path. If the chunk loaded a pre-cooked
			// PhysX blob from disk, we still need to build the visual mesh
			// (the SDF density was loaded but the visible triangle list
			// wasn't), but we can SKIP the cook entirely - just attach the
			// pre-cooked bytes via createTriangleMesh/createShape.
			if (chunk->HasCachedCookedCollision())
			{
				if (kickedOff >= chunkBudget)
				{
					anyRemaining = true;
					continue;
				}
				chunk->RebuildMesh(_marchingCubes, false);
				if (_gpuVisualsEnabled)
					chunk->SetVisualMeshHidden(true);
				if (chunk->AttachCachedCookedCollision())
				{
					++cacheHits;
					++kickedOff;  // counts against budget so we don't hog the frame
				}
				continue;
			}

			if (kickedOff >= chunkBudget)
			{
				anyRemaining = true;
				continue;
			}

			// Need the chunk's visual mesh to exist before the collision mesh
			// can be derived. RebuildMesh(false) updates the mesh + skips the
			// synchronous collision build (we kick collision async right after).
			chunk->RebuildMesh(_marchingCubes, false);
			if (_gpuVisualsEnabled)
				chunk->SetVisualMeshHidden(true);

			if (chunk->BeginAsyncCollisionRebuild())
			{
				++kickedOff;
				anyRemaining = true;  // cook just started, will need a poll next tick
			}
		}

		if (kickedOff > 0 || finalised > 0 || cacheHits > 0)
		{
			const double stepMs = std::chrono::duration<double, std::milli>(clk::now() - stepStart).count();
			LOG_INFO("VolumetricTerrain::ProcessCollisionRefreshStep: %d cache-hit(s), %d cook(s) kicked off, %d cook(s) finalised, step=%.1fms  remaining=%d",
				cacheHits, kickedOff - cacheHits, finalised, stepMs, anyRemaining ? 1 : 0);
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
			blob.hasMaterialEdits = chunk->HasMaterialEdits();
			if (blob.hasMaterialEdits)
			{
				blob.materials = chunk->GetMaterials();
				blob.materialWeights = chunk->GetMaterialWeights();
			}
			chunks.push_back(std::move(blob));
		}
	}

	void VolumetricTerrain::ApplyEditedChunkData(const std::vector<EditedChunkBlob>& chunks)
	{
		for (const auto& blob : chunks)
		{
			if (auto it = _chunks.find(blob.coord); it != _chunks.end() && it->second != nullptr)
			{
				if (blob.hasMaterialEdits)
				{
					it->second->SetEditedData(blob.densities, blob.materials, blob.materialWeights);
				}
				else
				{
					it->second->SetEditedData(blob.densities, {}, {});
				}
				MarkNeighbourChunksDirty(blob.coord, it->second->GetBounds().Center, 0.0f);
			}
		}

		StitchChunkBorders();
	}

	void VolumetricTerrain::Serialize(json& data, JsonFile* file)
	{
		if (_pendingCollisionRefresh)
		{
			StitchChunkBorders();
		}

		json& terrain = data["volumetricTerrain"];
		terrain["version"] = 2;
		_params.Serialize(terrain["generation"], file);

		const bool persistentFile = (file != nullptr && !file->GetAbsolutePath().empty());
		if (!persistentFile)
		{
			terrain["editedChunks"] = json::array();
			return;
		}

		// Save EVERY chunk's data as a baked snapshot. Loading the scene then
		// becomes a parallel memcpy + GPU upload per chunk instead of running
		// the SDF evaluator across every voxel of every chunk; for 100 chunks
		// @ res 40 this turns a ~10s synchronous load into ~150ms of CPU work
		// (the GPU upload phase, which is now the bottleneck).
		//
		// Storage cost: densities are quantised to uint16, materials are uint8,
		// the per-chunk payload is Brotli-compressed inside
		// SerializeEditedChunkBlobBinary - so the on-disk hit is moderate
		// (~5-15MB for a 100-chunk scene) and tractable for the asset packer's
		// pipeline. Worth it for the load-time win.
		json bakedChunks = json::array();
		bakedChunks.get_ptr<json::array_t*>()->reserve(_chunks.size());
		for (const auto& [coord, chunk] : _chunks)
		{
			if (chunk == nullptr)
				continue;

			EditedChunkBlob blob;
			blob.coord = coord;
			blob.densities = chunk->GetDensities();
			blob.materials = chunk->GetMaterials();
			blob.materialWeights = chunk->GetMaterialWeights();
			blob.hasMaterialEdits = chunk->HasMaterialEdits();
			// Pull the cached cooked-PhysX blob too, if the chunk has one.
			// Capturing here means the .hscene save embeds the cook output
			// alongside the density snapshot - so the next load skips both
			// SDF generation AND PxCookTriangleMesh.
			blob.cookedCollisionBlob = chunk->GetCachedCookedCollisionBlob();

			json out;
			out["coord"] = { coord.x, coord.y, coord.z };
			std::string payload;
			if (SerializeEditedChunkBlobBinary(blob, payload))
			{
				out["payload"] = std::move(payload);
			}
			else
			{
				const QuantizedDensityBlob packed = QuantizeDensities(blob.densities);
				out["densityMin"] = packed.minValue;
				out["densityMax"] = packed.maxValue;
				out["densitiesQ"] = packed.values;
				out["hasMaterialEdits"] = blob.hasMaterialEdits;
				if (blob.hasMaterialEdits)
				{
					out["materials"] = blob.materials;
					out["materialWeights"] = blob.materialWeights;
				}
			}
			bakedChunks.push_back(std::move(out));
		}

		terrain["bakedChunks"] = std::move(bakedChunks);
		// editedChunks key remains for back-compat with v2 scenes that only
		// have edits (no full bake). Empty in the new format - everything is
		// in bakedChunks now.
		terrain["editedChunks"] = json::array();
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

		// Pre-Initialize phase: parse the baked snapshot (if present) into
		// _bakedChunks BEFORE Initialize runs. BuildChunks consults this
		// map per chunk and skips SDF generation when there's a hit. Done
		// before Initialize because Initialize -> BuildChunks needs to see
		// the populated map.
		auto decodeBlob = [](const json& item, EditedChunkBlob& chunk) -> bool
		{
			if (!item.is_object() || !item.contains("coord") || !item["coord"].is_array())
				return false;
			chunk.coord.x = item["coord"][0].get<int32_t>();
			chunk.coord.y = item["coord"][1].get<int32_t>();
			chunk.coord.z = item["coord"][2].get<int32_t>();
			if (item.contains("payload") && item["payload"].is_string())
			{
				if (!DeserializeEditedChunkBlobBinary(item["payload"].get<std::string>(), chunk))
					return false;
			}
			else
			{
				chunk.hasMaterialEdits = item.value("hasMaterialEdits", item.contains("materialWeights") || item.contains("materials"));
				if (item.contains("densitiesQ"))
				{
					QuantizedDensityBlob packed;
					packed.minValue = item.value("densityMin", 0.0f);
					packed.maxValue = item.value("densityMax", packed.minValue);
					packed.values = item["densitiesQ"].get<std::vector<uint16_t>>();
					chunk.densities = DequantizeDensities(packed);
				}
				else if (item.contains("densities"))
				{
					chunk.densities = item["densities"].get<std::vector<float>>();
				}
				else
				{
					return false;
				}
				if (chunk.hasMaterialEdits)
				{
					if (item.contains("materials"))
						chunk.materials = item["materials"].get<std::vector<uint8_t>>();
					if (item.contains("materialWeights"))
						chunk.materialWeights = item["materialWeights"].get<std::vector<uint8_t>>();
				}
			}
			return true;
		};

		_bakedChunks.clear();
		if (terrain.contains("bakedChunks") && terrain["bakedChunks"].is_array())
		{
			for (const auto& item : terrain["bakedChunks"])
			{
				EditedChunkBlob chunk;
				if (decodeBlob(item, chunk))
					_bakedChunks[chunk.coord] = std::move(chunk);
			}
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
				if (item.contains("payload") && item["payload"].is_string())
				{
					if (!DeserializeEditedChunkBlobBinary(item["payload"].get<std::string>(), chunk))
					{
						continue;
					}
				}
				else
				{
					chunk.hasMaterialEdits = item.value("hasMaterialEdits", item.contains("materialWeights") || item.contains("materials"));
					if (item.contains("densitiesQ"))
					{
						QuantizedDensityBlob packed;
						packed.minValue = item.value("densityMin", 0.0f);
						packed.maxValue = item.value("densityMax", packed.minValue);
						packed.values = item["densitiesQ"].get<std::vector<uint16_t>>();
						chunk.densities = DequantizeDensities(packed);
					}
					else if (item.contains("densities"))
					{
						chunk.densities = item["densities"].get<std::vector<float>>();
					}
					if (chunk.hasMaterialEdits)
					{
						if (item.contains("materials")) chunk.materials = item["materials"].get<std::vector<uint8_t>>();
						if (item.contains("materialWeights")) chunk.materialWeights = item["materialWeights"].get<std::vector<uint8_t>>();
					}
				}
				edited.push_back(std::move(chunk));
			}
		}

		if (!edited.empty())
		{
			ApplyEditedChunkData(edited);
			BuildGpuVisualsOrFallback(true);
		}
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
		if (!_gpuVisualsEnabled || (renderFlags & MeshRenderFlags::MeshRenderNormal) == 0 || (renderFlags & MeshRenderFlags::MeshRenderTransparency) != 0 || (renderFlags & MeshRenderFlags::MeshRenderShadowMap) != 0)
		{
			return;
		}

		if (camera == nullptr || camera->GetEntity() == nullptr)
		{
			return;
		}

		const auto& frustum = camera->GetFrustum();
		const math::Vector3 cameraPosition = camera->GetEntity()->GetPosition();
		const float farDistance = std::max(0.0f, camera->GetFarZ());
		const float chunkSize = std::max(1.0f, _params.chunkWorldSize);
		const float lod0Distance = chunkSize * 2.5f;
		const float lod1Distance = chunkSize * 5.5f;

		for (auto& [coord, chunk] : _chunks)
		{
			(void)coord;
			if (chunk == nullptr)
			{
				continue;
			}

			const auto& bounds = chunk->GetBounds();
			if (!frustum.Intersects(bounds))
			{
				continue;
			}

			const math::Vector3 toChunk = bounds.Center - cameraPosition;
			const float chunkRadius = math::Vector3(bounds.Extents).Length();
			const float maxDistance = farDistance + chunkRadius;
			if (farDistance > 0.0f && toChunk.LengthSquared() > (maxDistance * maxDistance))
			{
				continue;
			}

			const float chunkDistance = sqrtf(toChunk.LengthSquared());
			uint32_t lodIndex = 0;
			if (chunkDistance > lod1Distance)
			{
				lodIndex = 2;
			}
			else if (chunkDistance > lod0Distance)
			{
				lodIndex = 1;
			}

			chunk->RenderGpuSurface(lodIndex);
		}
	}
}
