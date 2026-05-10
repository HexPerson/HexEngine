#include "VolumetricTerrainChunk.hpp"
#include <cstring>

namespace HexEngine::VolumetricTerrain
{
	namespace
	{
		struct GpuBrushParams
		{
			float brushCenterRadius[4] = { 0, 0, 0, 1 };
			float brushParams0[4] = { 1, 0.016f, 1, 1 }; // strength, dt, hardness, falloff
			float chunkOriginVoxel[4] = { 0, 0, 0, 1 }; // xyz origin, w voxel size
			int32_t intParams0[4] = { 0, 1, 0, 0 }; // mode, points, minX, minY
			int32_t intParams1[4] = { 0, 1, 1, 1 }; // minZ, maxX, maxY, maxZ
			float targetHeight = 0.0f;
			float pad0 = 0.0f;
			float pad1 = 0.0f;
			float pad2 = 0.0f;
		};
		static_assert((sizeof(GpuBrushParams) % 16) == 0, "GpuBrushParams must be 16-byte aligned for D3D11 constant buffers.");

		IShaderStage* g_gpuBrushStage = nullptr;
		IConstantBuffer* g_gpuBrushConstantBuffer = nullptr;
		bool g_gpuBrushInitTried = false;

		struct GpuSurfaceChunkParams
		{
			float chunkOriginVoxel[4] = { 0, 0, 0, 1 };
			float worldInfo[4] = { 0, 1, 1, 0 };
			float grassColor[4] = { 0.42f, 0.57f, 0.28f, 1.0f };
			float rockColor[4] = { 0.46f, 0.42f, 0.38f, 1.0f };
			float snowColor[4] = { 0.92f, 0.94f, 0.97f, 1.0f };
		};
		static_assert((sizeof(GpuSurfaceChunkParams) % 16) == 0, "GpuSurfaceChunkParams must be 16-byte aligned.");

		IShaderStage* g_gpuSurfaceVs = nullptr;
		IShaderStage* g_gpuSurfacePs = nullptr;
		IShaderStage* g_gpuSurfaceExtractCs = nullptr;
		IConstantBuffer* g_gpuSurfaceChunkBuffer = nullptr;
		std::shared_ptr<IShader> g_gpuSurfaceShader;
		std::shared_ptr<IShader> g_gpuSurfaceExtractShader;
		bool g_gpuSurfaceInitTried = false;

		bool EnsureGpuSurfaceShaders()
		{
			if (g_gpuSurfaceInitTried)
			{
				return g_gpuSurfaceVs != nullptr && g_gpuSurfacePs != nullptr && g_gpuSurfaceExtractCs != nullptr && g_gpuSurfaceChunkBuffer != nullptr;
			}

			g_gpuSurfaceInitTried = true;

			g_gpuSurfaceShader = IShader::Create("EngineData.Shaders/VolumetricTerrainSurface.hcs");
			g_gpuSurfaceExtractShader = IShader::Create("EngineData.Shaders/VolumetricTerrainExtract.hcs");
			if (g_gpuSurfaceShader != nullptr)
			{
				g_gpuSurfaceVs = g_gpuSurfaceShader->GetShaderStage(ShaderStage::VertexShader);
				g_gpuSurfacePs = g_gpuSurfaceShader->GetShaderStage(ShaderStage::PixelShader);
			}
			if (g_gpuSurfaceExtractShader != nullptr)
			{
				g_gpuSurfaceExtractCs = g_gpuSurfaceExtractShader->GetShaderStage(ShaderStage::ComputeShader);
			}
			g_gpuSurfaceChunkBuffer = g_pEnv->_graphicsDevice->CreateConstantBuffer(static_cast<uint32_t>(sizeof(GpuSurfaceChunkParams)));
			return g_gpuSurfaceVs != nullptr && g_gpuSurfacePs != nullptr && g_gpuSurfaceExtractCs != nullptr && g_gpuSurfaceChunkBuffer != nullptr;
		}

		bool EnsureGpuBrushKernel()
		{
			if (g_gpuBrushInitTried)
			{
				return g_gpuBrushStage != nullptr && g_gpuBrushConstantBuffer != nullptr;
			}

			g_gpuBrushInitTried = true;

			static const char* kBrushComputeSource = R"(
cbuffer BrushData : register(b0)
{
    float4 BrushCenterRadius;
    float4 BrushParams0;
    float4 ChunkOriginVoxel;
    int4 IntParams0;
    int4 IntParams1;
    float TargetHeight;
    float3 Padding0;
};
RWTexture3D<float> DensityField : register(u0);
float SmoothMinField(float a, float b, float k)
{
    if (k <= 0.0001) return min(a, b);
    float h = saturate(0.5 + 0.5 * (b - a) / k);
    return lerp(b, a, h) - k * h * (1.0 - h);
}
float SmoothMaxField(float a, float b, float k)
{
    return -SmoothMinField(-a, -b, k);
}
[numthreads(8, 8, 8)]
void MainCS(uint3 id : SV_DispatchThreadID)
{
    const float3 BrushCenter = BrushCenterRadius.xyz;
    const float BrushRadius = BrushCenterRadius.w;
    const float BrushStrength = BrushParams0.x;
    const float DeltaTime = BrushParams0.y;
    const float Hardness = BrushParams0.z;
    const float FalloffPower = BrushParams0.w;
    const float3 ChunkOrigin = ChunkOriginVoxel.xyz;
    const float VoxelSize = ChunkOriginVoxel.w;
    const int BrushMode = IntParams0.x;
    const int PointsPerAxis = IntParams0.y;
    const int MinX = IntParams0.z;
    const int MinY = IntParams0.w;
    const int MinZ = IntParams1.x;
    const int MaxX = IntParams1.y;
    const int MaxY = IntParams1.z;
    const int MaxZ = IntParams1.w;

    int3 voxel = int3(id.x + MinX, id.y + MinY, id.z + MinZ);
    if (voxel.x >= MaxX || voxel.y >= MaxY || voxel.z >= MaxZ) return;
    if (voxel.x < 0 || voxel.y < 0 || voxel.z < 0 || voxel.x >= PointsPerAxis || voxel.y >= PointsPerAxis || voxel.z >= PointsPerAxis) return;
    float3 worldPos = ChunkOrigin + float3(voxel) * VoxelSize;
    float3 dv = worldPos - BrushCenter;
    float dist = length(dv);
    if (dist > BrushRadius) return;
    float t = saturate(1.0 - (dist / max(BrushRadius, 0.0001)));
    float f = pow(t, max(0.1, FalloffPower));
    float h = lerp(0.15, 1.0, saturate(Hardness));
    float amount = BrushStrength * f * h * max(0.001, DeltaTime);
    float sphereSdf = dist - BrushRadius;
    float smoothK = max(VoxelSize * lerp(0.35, 2.25, 1.0 - saturate(Hardness)), 0.0001);
    float density = DensityField[voxel];
    if (BrushMode == 0)
    {
        float addAmount = amount * 0.12;
        float addSmoothK = smoothK * 2.35;
        float targetSolid = sphereSdf - addAmount;
        float blended = SmoothMinField(density, targetSolid, addSmoothK);
        density = lerp(density, blended, saturate(0.16 + (f * 0.42)));
    }
    else if (BrushMode == 1 || BrushMode == 7)
    {
        float targetEmpty = -sphereSdf + (amount * 0.35);
        density = SmoothMaxField(density, targetEmpty, smoothK);
    }
    else if (BrushMode == 2) density -= amount;
    else if (BrushMode == 3)
    {
        float targetDensity = worldPos.y - TargetHeight;
        float blend = saturate(f * max(0.02, DeltaTime * BrushStrength));
        density = lerp(density, targetDensity, blend);
    }
    DensityField[voxel] = density;
}
)";

			g_gpuBrushStage = g_pEnv->_graphicsDevice->CreateComputeShaderFromSource(kBrushComputeSource, "MainCS");
			const uint32_t cbSize = (static_cast<uint32_t>(sizeof(GpuBrushParams)) + 15u) & ~15u;
			g_gpuBrushConstantBuffer = g_pEnv->_graphicsDevice->CreateConstantBuffer(cbSize);
			return g_gpuBrushStage != nullptr && g_gpuBrushConstantBuffer != nullptr;
		}
	}

	VolumetricTerrainChunk::~VolumetricTerrainChunk()
	{
		ReleaseGpuSurfaceResources();
		ReleaseGpuDensityResources();
		ReleaseEntityReferences(true);
	}

	VolumetricTerrainChunk::VolumetricTerrainChunk(
		const ChunkCoord& coord,
		const SdfTerrainGenerationParams& params,
		const math::Vector3& origin,
		Entity* parentEntity) :
		_coord(coord),
		_params(params),
		_origin(origin)
	{
		_voxelSize = _params.chunkWorldSize / static_cast<float>(std::max(1, _params.chunkResolution));

		_bounds.Center = _origin + math::Vector3(_params.chunkWorldSize * 0.5f, _params.chunkWorldSize * 0.5f, _params.chunkWorldSize * 0.5f);
		_bounds.Extents = math::Vector3(_params.chunkWorldSize * 0.5f, _params.chunkWorldSize * 0.5f, _params.chunkWorldSize * 0.5f);

		const int32_t points = _params.chunkResolution + 1;
		const size_t voxelCount = static_cast<size_t>(points * points * points);
		_densities.resize(voxelCount, 1.0f);
		_materials.resize(voxelCount, 0);

		if (parentEntity != nullptr && parentEntity->GetScene() != nullptr)
		{
			const std::string chunkName = std::format("VolTerrainChunk_{}_{}_{}", coord.x, coord.y, coord.z);
			_entity = parentEntity->GetScene()->CreateEntity(chunkName, _origin);
			if (_entity != nullptr)
			{
				_entity->SetParent(parentEntity);
				_entity->SetFlag(EntityFlags::ExcludeFromHLOD);
				_meshComponent = _entity->AddComponent<StaticMeshComponent>();
				_rigidBody = _entity->AddComponent<RigidBody>(IRigidBody::BodyType::Static);
			}
		}
	}

	void VolumetricTerrainChunk::Generate(SdfTerrainGenerator& generator)
	{
		const int32_t points = _params.chunkResolution + 1;
		const math::Vector3 center = _origin + math::Vector3(_params.chunkWorldSize * 0.5f, 0.0f, _params.chunkWorldSize * 0.5f);
		const auto heightMap = generator.GenerateHeightSeedMap(center, _params.chunkResolution, _params.chunkWorldSize);

		for (int32_t z = 0; z < points; ++z)
		{
			for (int32_t y = 0; y < points; ++y)
			{
				for (int32_t x = 0; x < points; ++x)
				{
					const math::Vector3 wp = DensityToWorld(x, y, z);
					const float h = generator.SampleHeightFromMap(heightMap, _params.chunkResolution, x, z);
					const float d = generator.SampleDensity(wp, h);
					_densities[static_cast<size_t>(Index(x, y, z))] = d;

					uint8_t material = 0;
					if (wp.y > (_params.chunkWorldSize * _params.chunksY * 0.6f)) material = 2;
					else if (wp.y > (_params.chunkWorldSize * _params.chunksY * 0.35f)) material = 1;
					_materials[static_cast<size_t>(Index(x, y, z))] = material;
				}
			}
		}

		_generated = true;
		_densityDirty = false;
		_meshDirty = true;
		_collisionDirty = true;
		_materialDirty = false;
		UploadDensityToGpu();
	}

	int32_t VolumetricTerrainChunk::Index(int32_t x, int32_t y, int32_t z) const
	{
		const int32_t points = _params.chunkResolution + 1;
		return (z * points * points) + (y * points) + x;
	}

	float VolumetricTerrainChunk::ComputeFalloff(float distance, float radius, float falloffPower) const
	{
		if (radius <= 0.0f)
			return 0.0f;

		const float t = std::clamp(1.0f - (distance / radius), 0.0f, 1.0f);
		return powf(t, std::max(0.1f, falloffPower));
	}

	std::vector<float> VolumetricTerrainChunk::BuildCollisionDensityField(int32_t collisionResolution) const
	{
		const int32_t srcResolution = std::max(1, _params.chunkResolution);
		const int32_t dstResolution = std::clamp(collisionResolution, 1, srcResolution);
		const int32_t srcPoints = srcResolution + 1;
		const int32_t dstPoints = dstResolution + 1;
		std::vector<float> coarseDensities(static_cast<size_t>(dstPoints * dstPoints * dstPoints), 1.0f);

		auto srcIndex = [srcPoints](int32_t x, int32_t y, int32_t z)
		{
			return (z * srcPoints * srcPoints) + (y * srcPoints) + x;
		};

		auto dstIndex = [dstPoints](int32_t x, int32_t y, int32_t z)
		{
			return (z * dstPoints * dstPoints) + (y * dstPoints) + x;
		};

		auto sampleDensity = [&](float fx, float fy, float fz)
		{
			const float cx = std::clamp(fx, 0.0f, static_cast<float>(srcResolution));
			const float cy = std::clamp(fy, 0.0f, static_cast<float>(srcResolution));
			const float cz = std::clamp(fz, 0.0f, static_cast<float>(srcResolution));

			const int32_t x0 = static_cast<int32_t>(floorf(cx));
			const int32_t y0 = static_cast<int32_t>(floorf(cy));
			const int32_t z0 = static_cast<int32_t>(floorf(cz));
			const int32_t x1 = std::min(x0 + 1, srcResolution);
			const int32_t y1 = std::min(y0 + 1, srcResolution);
			const int32_t z1 = std::min(z0 + 1, srcResolution);

			const float tx = cx - static_cast<float>(x0);
			const float ty = cy - static_cast<float>(y0);
			const float tz = cz - static_cast<float>(z0);

			const float c000 = _densities[static_cast<size_t>(srcIndex(x0, y0, z0))];
			const float c100 = _densities[static_cast<size_t>(srcIndex(x1, y0, z0))];
			const float c010 = _densities[static_cast<size_t>(srcIndex(x0, y1, z0))];
			const float c110 = _densities[static_cast<size_t>(srcIndex(x1, y1, z0))];
			const float c001 = _densities[static_cast<size_t>(srcIndex(x0, y0, z1))];
			const float c101 = _densities[static_cast<size_t>(srcIndex(x1, y0, z1))];
			const float c011 = _densities[static_cast<size_t>(srcIndex(x0, y1, z1))];
			const float c111 = _densities[static_cast<size_t>(srcIndex(x1, y1, z1))];

			const float c00 = std::lerp(c000, c100, tx);
			const float c10 = std::lerp(c010, c110, tx);
			const float c01 = std::lerp(c001, c101, tx);
			const float c11 = std::lerp(c011, c111, tx);
			const float c0 = std::lerp(c00, c10, ty);
			const float c1 = std::lerp(c01, c11, ty);
			return std::lerp(c0, c1, tz);
		};

		for (int32_t z = 0; z < dstPoints; ++z)
		{
			for (int32_t y = 0; y < dstPoints; ++y)
			{
				for (int32_t x = 0; x < dstPoints; ++x)
				{
					const float fx = (static_cast<float>(x) / static_cast<float>(dstResolution)) * static_cast<float>(srcResolution);
					const float fy = (static_cast<float>(y) / static_cast<float>(dstResolution)) * static_cast<float>(srcResolution);
					const float fz = (static_cast<float>(z) / static_cast<float>(dstResolution)) * static_cast<float>(srcResolution);
					coarseDensities[static_cast<size_t>(dstIndex(x, y, z))] = sampleDensity(fx, fy, fz);
				}
			}
		}

		return coarseDensities;
	}

	float VolumetricTerrainChunk::SmoothMin(float a, float b, float k) const
	{
		if (k <= 0.0001f)
		{
			return std::min(a, b);
		}

		const float h = std::clamp(0.5f + (0.5f * (b - a) / k), 0.0f, 1.0f);
		return std::lerp(b, a, h) - (k * h * (1.0f - h));
	}

	float VolumetricTerrainChunk::SmoothMax(float a, float b, float k) const
	{
		return -SmoothMin(-a, -b, k);
	}

	void VolumetricTerrainChunk::RelaxEditedRegion(
		const math::Vector3& center,
		float radius,
		float hardness,
		int32_t minX,
		int32_t maxX,
		int32_t minY,
		int32_t maxY,
		int32_t minZ,
		int32_t maxZ)
	{
		const int32_t points = _params.chunkResolution + 1;
		if (_densities.empty())
		{
			return;
		}

		const int32_t rx0 = std::clamp(minX - 1, 0, points - 1);
		const int32_t rx1 = std::clamp(maxX + 1, 0, points - 1);
		const int32_t ry0 = std::clamp(minY - 1, 0, points - 1);
		const int32_t ry1 = std::clamp(maxY + 1, 0, points - 1);
		const int32_t rz0 = std::clamp(minZ - 1, 0, points - 1);
		const int32_t rz1 = std::clamp(maxZ + 1, 0, points - 1);
		const std::vector<float> source = _densities;
		const float relaxStrength = std::lerp(0.38f, 0.12f, std::clamp(hardness, 0.0f, 1.0f));
		const float affectRadius = radius + (_voxelSize * 1.75f);

		for (int32_t z = rz0; z <= rz1; ++z)
		{
			for (int32_t y = ry0; y <= ry1; ++y)
			{
				for (int32_t x = rx0; x <= rx1; ++x)
				{
					const math::Vector3 wp = DensityToWorld(x, y, z);
					const float dist = (wp - center).Length();
					if (dist > affectRadius)
					{
						continue;
					}

					const size_t idx = static_cast<size_t>(Index(x, y, z));
					const float current = source[idx];
					float accum = current;
					int32_t count = 1;

					static constexpr int32_t offsets[6][3] =
					{
						{ 1, 0, 0 }, { -1, 0, 0 },
						{ 0, 1, 0 }, { 0, -1, 0 },
						{ 0, 0, 1 }, { 0, 0, -1 }
					};

					for (const auto& o : offsets)
					{
						const int32_t sx = std::clamp(x + o[0], 0, points - 1);
						const int32_t sy = std::clamp(y + o[1], 0, points - 1);
						const int32_t sz = std::clamp(z + o[2], 0, points - 1);
						accum += source[static_cast<size_t>(Index(sx, sy, sz))];
						++count;
					}

					const float average = accum / static_cast<float>(count);
					const float edgeFade = std::clamp(1.0f - (dist / std::max(affectRadius, 0.0001f)), 0.0f, 1.0f);
					_densities[idx] = std::lerp(current, average, relaxStrength * edgeFade);
				}
			}
		}
	}

	math::Vector3 VolumetricTerrainChunk::DensityToWorld(int32_t x, int32_t y, int32_t z) const
	{
		return _origin + math::Vector3(
			static_cast<float>(x) * _voxelSize,
			static_cast<float>(y) * _voxelSize,
			static_cast<float>(z) * _voxelSize);
	}

	bool VolumetricTerrainChunk::ApplyBrush(const math::Vector3& center, const BrushSettings& settings, float deltaTime)
	{
		if (ApplyBrushGpu(center, settings, deltaTime))
		{
			BrushSettings cpuMirror = settings;
			cpuMirror.useGpu = false;
			ApplyBrushCpu(center, cpuMirror, deltaTime, false, false);

			_hasEdits = true;
			_densityDirty = false;
			_meshDirty = true;
			_collisionDirty = true;
			return true;
		}

		return ApplyBrushCpu(center, settings, deltaTime, true, true);
	}

	bool VolumetricTerrainChunk::ApplyBrushCpu(const math::Vector3& center, const BrushSettings& settings, float deltaTime, bool uploadGpuAfterModify, bool runRelaxation)
	{
		const int32_t points = _params.chunkResolution + 1;
		const float radiusSq = settings.radius * settings.radius;
		bool modified = false;

		const math::Vector3 localCenter = (center - _origin) / std::max(0.0001f, _voxelSize);
		const int32_t vxRadius = std::max(1, static_cast<int32_t>(ceilf(settings.radius / std::max(0.0001f, _voxelSize))));
		const int32_t minX = std::clamp(static_cast<int32_t>(floorf(localCenter.x)) - vxRadius, 0, points - 1);
		const int32_t maxX = std::clamp(static_cast<int32_t>(ceilf(localCenter.x)) + vxRadius, 0, points - 1);
		const int32_t minY = std::clamp(static_cast<int32_t>(floorf(localCenter.y)) - vxRadius, 0, points - 1);
		const int32_t maxY = std::clamp(static_cast<int32_t>(ceilf(localCenter.y)) + vxRadius, 0, points - 1);
		const int32_t minZ = std::clamp(static_cast<int32_t>(floorf(localCenter.z)) - vxRadius, 0, points - 1);
		const int32_t maxZ = std::clamp(static_cast<int32_t>(ceilf(localCenter.z)) + vxRadius, 0, points - 1);

		for (int32_t z = minZ; z <= maxZ; ++z)
		{
			for (int32_t y = minY; y <= maxY; ++y)
			{
				for (int32_t x = minX; x <= maxX; ++x)
				{
					const math::Vector3 wp = DensityToWorld(x, y, z);
					const math::Vector3 diff = wp - center;
					const float distSq = diff.LengthSquared();
					if (distSq > radiusSq)
						continue;

					const float dist = sqrtf(distSq);
					const float falloff = ComputeFalloff(dist, settings.radius, settings.falloff);
					if (falloff <= 0.0f)
						continue;

					const float hard = std::lerp(0.15f, 1.0f, std::clamp(settings.hardness, 0.0f, 1.0f));
					const float delta = settings.strength * falloff * hard * std::max(0.001f, deltaTime);
					const float sphereSdf = dist - settings.radius;
					const float brushOffset = delta * 0.35f;
					const float smoothK = std::max(_voxelSize * std::lerp(0.85f, 3.25f, 1.0f - std::clamp(settings.hardness, 0.0f, 1.0f)), 0.0001f);
					float& density = _densities[static_cast<size_t>(Index(x, y, z))];

					switch (settings.mode)
					{
					case BrushMode::Add:
					{
						const float addOffset = delta * 0.12f;
						const float addSmoothK = smoothK * 2.35f;
						const float targetSolid = sphereSdf - addOffset;
						const float blended = SmoothMin(density, targetSolid, addSmoothK);
						density = std::lerp(density, blended, std::clamp(0.16f + (falloff * 0.42f), 0.0f, 0.85f));
						break;
					}
					case BrushMode::Elevate:
						density -= delta;
						break;
					case BrushMode::Subtract:
					case BrushMode::Tunnel:
					{
						const float targetEmpty = -sphereSdf + brushOffset;
						density = SmoothMax(density, targetEmpty, smoothK);
						break;
					}
					case BrushMode::Flatten:
					{
						const float target = wp.y - settings.targetHeight;
						density = std::lerp(density, target, std::clamp(falloff * std::max(0.02f, deltaTime * settings.strength), 0.0f, 1.0f));
						break;
					}
					case BrushMode::Smooth:
					{
						float accum = 0.0f;
						int32_t count = 0;
						for (int32_t oz = -1; oz <= 1; ++oz)
						{
							for (int32_t oy = -1; oy <= 1; ++oy)
							{
								for (int32_t ox = -1; ox <= 1; ++ox)
								{
									const int32_t sx = std::clamp(x + ox, 0, points - 1);
									const int32_t sy = std::clamp(y + oy, 0, points - 1);
									const int32_t sz = std::clamp(z + oz, 0, points - 1);
									accum += _densities[static_cast<size_t>(Index(sx, sy, sz))];
									++count;
								}
							}
						}
						if (count > 0)
						{
							density = std::lerp(density, accum / static_cast<float>(count), std::clamp(falloff * 0.45f, 0.0f, 1.0f));
						}
						break;
					}
					case BrushMode::Erode:
					{
						const float targetEmpty = -sphereSdf + (brushOffset * 0.35f);
						density = std::lerp(density, SmoothMax(density, targetEmpty, smoothK), std::clamp(falloff * 0.5f * hard, 0.0f, 1.0f));
						break;
					}
					case BrushMode::PaintMaterial:
						_materials[static_cast<size_t>(Index(x, y, z))] = static_cast<uint8_t>(std::clamp(settings.materialIndex, 0, 255));
						_materialDirty = true;
						break;
					case BrushMode::Noise:
					{
						const float n = sinf((wp.x + wp.y + wp.z) * std::max(0.01f, settings.noiseScale) * 0.25f);
						density += n * delta;
						break;
					}
					default:
						break;
					}

					modified = true;
				}
			}
		}

		if (modified)
		{
			if (runRelaxation && (settings.mode == BrushMode::Add ||
				settings.mode == BrushMode::Subtract ||
				settings.mode == BrushMode::Tunnel ||
				settings.mode == BrushMode::Erode))
			{
				const float relaxRadiusA = settings.mode == BrushMode::Add ? settings.radius * 1.2f : settings.radius;
				const float relaxRadiusB = settings.mode == BrushMode::Add ? settings.radius * 1.4f : settings.radius * 1.1f;
				const float relaxHardnessA = settings.mode == BrushMode::Add ? std::max(0.0f, settings.hardness - 0.2f) : settings.hardness;
				const float relaxHardnessB = settings.mode == BrushMode::Add ? std::clamp(settings.hardness - 0.05f, 0.0f, 1.0f) : std::min(settings.hardness + 0.15f, 1.0f);
				RelaxEditedRegion(center, relaxRadiusA, relaxHardnessA, minX, maxX, minY, maxY, minZ, maxZ);
				RelaxEditedRegion(center, relaxRadiusB, relaxHardnessB, minX, maxX, minY, maxY, minZ, maxZ);
			}

			_hasEdits = true;
			_densityDirty = false;
			_meshDirty = true;
			_collisionDirty = true;
			if (uploadGpuAfterModify)
			{
				UploadDensityToGpu();
			}
		}

		return modified;
	}

	void VolumetricTerrainChunk::RebuildMesh(const MarchingCubes& marchingCubes, bool rebuildCollision)
	{
		if (_meshComponent == nullptr || !_generated)
			return;

		auto output = marchingCubes.Build(_densities, _params.chunkResolution, _voxelSize, _origin, _params.uvScale);

		// Some chunks can be fully empty/solid and produce no surface triangles.
		// Do not create GPU buffers with zero byte widths.
		if (output.vertices.empty() || output.indices.empty())
		{
			_mesh.reset();
			_meshComponent->SetMesh(nullptr);

			if (_rigidBody != nullptr)
			{
				_rigidBody->RemoveCollider();
			}

			_meshDirty = false;
			_collisionDirty = false;
			return;
		}

		if (_mesh == nullptr)
		{
			_mesh = std::make_shared<Mesh>(nullptr, std::format("VolTerrainMesh_{}_{}_{}", _coord.x, _coord.y, _coord.z));
			if (!_mesh->CreateDynamicBuffers(static_cast<uint32_t>(output.vertices.size()), static_cast<uint32_t>(output.indices.size())))
			{
				_mesh.reset();
				return;
			}
		}

		if (!_mesh->IsDynamicMesh())
		{
			_mesh->Destroy();
			if (!_mesh->CreateDynamicBuffers(static_cast<uint32_t>(output.vertices.size()), static_cast<uint32_t>(output.indices.size())))
			{
				_mesh.reset();
				return;
			}
		}

		if (!_mesh->UpdateDynamicGeometry(output.vertices, output.indices))
		{
			return;
		}

		_mesh->SetAABB(output.aabb);
		_mesh->SetOBB(output.obb);
		if (_meshComponent->GetMesh() != _mesh)
		{
			_meshComponent->SetMesh(_mesh);
		}
		else if (_entity != nullptr)
		{
			_entity->SetAABB(output.aabb);
			_entity->SetOBB(output.obb);
			_entity->RecalculateBoundingVolumes(output.aabb);
		}
		if (_meshComponent->GetMaterial() == nullptr)
		{
			_meshComponent->SetMaterial(Material::Create("EngineData.Materials/Default.hmat"));
		}

		if (rebuildCollision)
		{
			RebuildCollision();
		}

		_meshDirty = false;
	}

	void VolumetricTerrainChunk::RebuildCollision()
	{
		if (_rigidBody == nullptr)
			return;

		const int32_t collisionResolution = std::clamp(_params.collisionResolution, 4, std::max(4, _params.chunkResolution));
		_rigidBody->RemoveCollider();
		if (_mesh == nullptr)
		{
			_collisionDirty = false;
			return;
		}

		if (collisionResolution >= _params.chunkResolution)
		{
			if (_mesh->GetNumFaces() > 0)
			{
				_rigidBody->AddTriangleMeshCollider(_mesh.get(), true);
			}
			_collisionDirty = false;
			return;
		}

		const std::vector<float> collisionDensities = BuildCollisionDensityField(collisionResolution);
		const float collisionVoxelSize = _params.chunkWorldSize / static_cast<float>(std::max(1, collisionResolution));
		MarchingCubes collisionMarching;
		auto collisionOutput = collisionMarching.Build(collisionDensities, collisionResolution, collisionVoxelSize, _origin, _params.uvScale);
		if (!collisionOutput.vertices.empty() && !collisionOutput.indices.empty())
		{
			std::vector<math::Vector3> collisionVertices;
			collisionVertices.reserve(collisionOutput.vertices.size());
			for (const auto& vertex : collisionOutput.vertices)
			{
				collisionVertices.emplace_back(vertex._position.x, vertex._position.y, vertex._position.z);
			}

			_rigidBody->AddTriangleMeshCollider(collisionVertices, collisionOutput.indices, static_cast<uint32_t>(collisionOutput.indices.size() / 3), true);
		}

		_collisionDirty = false;
	}

	void VolumetricTerrainChunk::MarkDirtyAll()
	{
		_densityDirty = true;
		_meshDirty = true;
		_collisionDirty = true;
		_materialDirty = true;
	}

	void VolumetricTerrainChunk::MarkDirtyMeshOnly()
	{
		_meshDirty = true;
		_collisionDirty = true;
	}

	void VolumetricTerrainChunk::SetEditedData(const std::vector<float>& densities, const std::vector<uint8_t>& materials)
	{
		if (densities.size() == _densities.size())
		{
			_densities = densities;
			UploadDensityToGpu();
			_hasEdits = true;
			_meshDirty = true;
			_collisionDirty = true;
		}

		if (materials.size() == _materials.size())
		{
			_materials = materials;
			_materialDirty = true;
		}
	}

	float VolumetricTerrainChunk::GetDensityAt(int32_t x, int32_t y, int32_t z) const
	{
		const int32_t points = _params.chunkResolution + 1;
		const int32_t sx = std::clamp(x, 0, points - 1);
		const int32_t sy = std::clamp(y, 0, points - 1);
		const int32_t sz = std::clamp(z, 0, points - 1);
		return _densities[static_cast<size_t>(Index(sx, sy, sz))];
	}

	void VolumetricTerrainChunk::SetDensityAt(int32_t x, int32_t y, int32_t z, float density)
	{
		const int32_t points = _params.chunkResolution + 1;
		const int32_t sx = std::clamp(x, 0, points - 1);
		const int32_t sy = std::clamp(y, 0, points - 1);
		const int32_t sz = std::clamp(z, 0, points - 1);
		_densities[static_cast<size_t>(Index(sx, sy, sz))] = density;
	}

	void VolumetricTerrainChunk::SyncDensityToGpu()
	{
		UploadDensityToGpu();
	}

	uint8_t VolumetricTerrainChunk::GetMaterialAt(int32_t x, int32_t y, int32_t z) const
	{
		const int32_t points = _params.chunkResolution + 1;
		const int32_t sx = std::clamp(x, 0, points - 1);
		const int32_t sy = std::clamp(y, 0, points - 1);
		const int32_t sz = std::clamp(z, 0, points - 1);
		return _materials[static_cast<size_t>(Index(sx, sy, sz))];
	}

	void VolumetricTerrainChunk::SetMaterialAt(int32_t x, int32_t y, int32_t z, uint8_t material)
	{
		const int32_t points = _params.chunkResolution + 1;
		const int32_t sx = std::clamp(x, 0, points - 1);
		const int32_t sy = std::clamp(y, 0, points - 1);
		const int32_t sz = std::clamp(z, 0, points - 1);
		_materials[static_cast<size_t>(Index(sx, sy, sz))] = material;
	}

	void VolumetricTerrainChunk::ReleaseEntityReferences(bool queueDeleteEntity)
	{
		if (queueDeleteEntity && _entity != nullptr && _entity->GetScene() != nullptr)
		{
			_entity->DeleteMe(false);
		}

		_mesh.reset();
		_meshComponent = nullptr;
		_rigidBody = nullptr;
		_entity = nullptr;
	}

	void VolumetricTerrainChunk::SetVisualMeshHidden(bool hidden)
	{
		if (_entity != nullptr)
		{
			if (hidden)
			{
				_entity->SetFlag(EntityFlags::DoNotRender);
			}
			else
			{
				_entity->ClearFlags(EntityFlags::DoNotRender);
			}
		}
	}

float VolumetricTerrainChunk::SampleDensityNearestWorld(const math::Vector3& worldPosition) const
{
	const int32_t points = _params.chunkResolution + 1;
	const math::Vector3 local = (worldPosition - _origin) / std::max(0.0001f, _voxelSize);
	const int32_t x = std::clamp(static_cast<int32_t>(std::lround(local.x)), 0, points - 1);
	const int32_t y = std::clamp(static_cast<int32_t>(std::lround(local.y)), 0, points - 1);
	const int32_t z = std::clamp(static_cast<int32_t>(std::lround(local.z)), 0, points - 1);
	return _densities[static_cast<size_t>(Index(x, y, z))];
}

bool VolumetricTerrainChunk::IsGpuBrushMode(BrushMode mode) const
{
	return mode == BrushMode::Add ||
		mode == BrushMode::Subtract ||
		mode == BrushMode::Elevate ||
		mode == BrushMode::Flatten ||
		mode == BrushMode::Tunnel;
}

bool VolumetricTerrainChunk::EnsureGpuSurfacePipeline()
{
	return EnsureGpuSurfaceShaders();
}

bool VolumetricTerrainChunk::EnsureGpuSurfaceResources()
{
	if (!EnsureGpuSurfacePipeline())
	{
		return false;
	}

	const uint32_t resolution = static_cast<uint32_t>(std::max(1, _params.chunkResolution));
	const uint32_t cellCount = resolution * resolution * resolution;
	// Marching tetrahedra can emit multiple triangles per cell under aggressive edits.
	// Keep generous headroom here to avoid append-buffer overflow corrupting the visible surface.
	const uint32_t desiredTriangleCapacity = std::max(cellCount * 12u, resolution * resolution * 96u);
	if (_gpuSurfaceTriangles != nullptr && _gpuSurfaceDrawArgs != nullptr && _gpuSurfaceTriangleCapacity == desiredTriangleCapacity)
	{
		return true;
	}

	ReleaseGpuSurfaceResources();

	struct DrawArgs
	{
		uint32_t vertexCountPerInstance;
		uint32_t instanceCount;
		uint32_t startVertexLocation;
		uint32_t startInstanceLocation;
	};

	struct GpuTerrainTriangle
	{
		float p0[4];
		float p1[4];
		float p2[4];
		float n0[4];
		float n1[4];
		float n2[4];
	};

	_gpuSurfaceTriangles = g_pEnv->_graphicsDevice->CreateStructuredBuffer(
		static_cast<uint32_t>(sizeof(GpuTerrainTriangle)),
		desiredTriangleCapacity,
		StructuredBufferFlags::ShaderResource | StructuredBufferFlags::UnorderedAccess | StructuredBufferFlags::AppendConsume);
	if (_gpuSurfaceTriangles == nullptr)
	{
		ReleaseGpuSurfaceResources();
		return false;
	}

	const DrawArgs initialArgs{ 3u, 0u, 0u, 0u };
	_gpuSurfaceDrawArgs = g_pEnv->_graphicsDevice->CreateStructuredBuffer(
		static_cast<uint32_t>(sizeof(DrawArgs)),
		1u,
		StructuredBufferFlags::DrawIndirectArgs,
		D3D11_USAGE_DEFAULT,
		0u,
		&initialArgs);
	if (_gpuSurfaceDrawArgs == nullptr)
	{
		ReleaseGpuSurfaceResources();
		return false;
	}

	_gpuSurfaceTriangleCapacity = desiredTriangleCapacity;
	return true;
}

void VolumetricTerrainChunk::ReleaseGpuSurfaceResources()
{
	SAFE_DELETE(_gpuSurfaceTriangles);
	SAFE_DELETE(_gpuSurfaceDrawArgs);
	_gpuSurfaceTriangleCapacity = 0;
	_gpuSurfaceReady = false;
}

bool VolumetricTerrainChunk::EnsureGpuDensityResources()
{
	const int32_t points = _params.chunkResolution + 1;
	if (points <= 1)
	{
		return false;
	}

	if (_gpuDensityTexture == nullptr)
	{
		_gpuDensityTexture = g_pEnv->_graphicsDevice->CreateTexture3D(
			points,
			points,
			points,
			DXGI_FORMAT_R32_FLOAT,
			1,
			D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
			1,
			1,
			0,
			nullptr,
			D3D11_RTV_DIMENSION_UNKNOWN,
			D3D11_UAV_DIMENSION_TEXTURE3D,
			D3D11_SRV_DIMENSION_TEXTURE3D,
			D3D11_DSV_DIMENSION_UNKNOWN);
	}

	return _gpuDensityTexture != nullptr && _gpuDensityTexture->SupportsRandomWrite();
}

bool VolumetricTerrainChunk::BuildGpuSurface()
{
	if (!_generated || !EnsureGpuDensityResources() || !EnsureGpuSurfaceResources() || _gpuDensityTexture == nullptr || _gpuSurfaceTriangles == nullptr || _gpuSurfaceDrawArgs == nullptr)
	{
		_gpuSurfaceReady = false;
		return false;
	}

	struct DrawArgs
	{
		uint32_t vertexCountPerInstance;
		uint32_t instanceCount;
		uint32_t startVertexLocation;
		uint32_t startInstanceLocation;
	};

	GpuSurfaceChunkParams params{};
	params.chunkOriginVoxel[0] = _origin.x;
	params.chunkOriginVoxel[1] = _origin.y;
	params.chunkOriginVoxel[2] = _origin.z;
	params.chunkOriginVoxel[3] = _voxelSize;
	params.worldInfo[0] = static_cast<float>(_params.chunkResolution);
	params.worldInfo[1] = static_cast<float>(_params.chunkWorldSize);
	params.worldInfo[2] = static_cast<float>(_params.chunkWorldSize * _params.chunksY);
	params.worldInfo[3] = static_cast<float>(_gpuSurfaceTriangleCapacity);
	g_gpuSurfaceChunkBuffer->Write(&params, sizeof(params));

	const DrawArgs initialArgs{ 3u, 0u, 0u, 0u };
	_gpuSurfaceDrawArgs->SetData(&initialArgs, sizeof(initialArgs));

	auto* graphics = g_pEnv->_graphicsDevice;
	graphics->SetConstantBufferCS(6, g_gpuSurfaceChunkBuffer);
	graphics->SetComputeTexture3D(0, _gpuDensityTexture);
	graphics->SetComputeRwStructuredBuffer(0, _gpuSurfaceTriangles, 0u);
	graphics->SetComputeShader(g_gpuSurfaceExtractCs);
	const uint32_t groups = static_cast<uint32_t>((std::max(1, _params.chunkResolution) + 3) / 4);
	graphics->Dispatch(groups, groups, groups);
	graphics->SetComputeShader(nullptr);
	graphics->ClearComputeTexture3D(0);
	graphics->ClearComputeRwStructuredBuffer(0);
	graphics->SetConstantBufferCS(6, nullptr);
	graphics->CopyStructureCount(_gpuSurfaceTriangles, _gpuSurfaceDrawArgs, sizeof(uint32_t));
	_gpuSurfaceReady = true;
	_meshDirty = false;
	return true;
}

void VolumetricTerrainChunk::ReleaseGpuDensityResources()
{
	SAFE_DELETE(_gpuDensityTexture);
}

void VolumetricTerrainChunk::UploadDensityToGpu()
{
	if (!EnsureGpuDensityResources())
	{
		return;
	}

	if (_gpuDensityTexture == nullptr || _densities.empty())
	{
		return;
	}
	_gpuDensityTexture->SetPixels(reinterpret_cast<uint8_t*>(_densities.data()), static_cast<uint32_t>(_densities.size() * sizeof(float)));
}

void VolumetricTerrainChunk::ReadbackDensityFromGpu()
{
	if (_gpuDensityTexture == nullptr)
	{
		return;
	}
	_gpuDensityTexture->GetPixels(_densities);
}

bool VolumetricTerrainChunk::ApplyBrushGpu(const math::Vector3& center, const BrushSettings& settings, float deltaTime)
{
	if (!settings.useGpu)
	{
		return false;
	}

	if (!IsGpuBrushMode(settings.mode))
	{
		return false;
	}

	if (!EnsureGpuBrushKernel() || !EnsureGpuDensityResources())
	{
		return false;
	}

	const int32_t points = _params.chunkResolution + 1;
	if (g_gpuBrushStage == nullptr || g_gpuBrushConstantBuffer == nullptr || _gpuDensityTexture == nullptr)
	{
		return false;
	}

	if (settings.radius <= 0.0f || settings.strength <= 0.0f)
	{
		return false;
	}

	const int32_t pointsMinusOne = std::max(1, _params.chunkResolution);
	const math::Vector3 localCenter = (center - _origin) / std::max(0.0001f, _voxelSize);
	const int32_t vxRadius = std::max(1, static_cast<int32_t>(ceilf(settings.radius / std::max(0.0001f, _voxelSize))));
	const int32_t minX = std::clamp(static_cast<int32_t>(floorf(localCenter.x)) - vxRadius, 0, pointsMinusOne);
	const int32_t maxX = std::clamp(static_cast<int32_t>(ceilf(localCenter.x)) + vxRadius + 1, 1, points);
	const int32_t minY = std::clamp(static_cast<int32_t>(floorf(localCenter.y)) - vxRadius, 0, pointsMinusOne);
	const int32_t maxY = std::clamp(static_cast<int32_t>(ceilf(localCenter.y)) + vxRadius + 1, 1, points);
	const int32_t minZ = std::clamp(static_cast<int32_t>(floorf(localCenter.z)) - vxRadius, 0, pointsMinusOne);
	const int32_t maxZ = std::clamp(static_cast<int32_t>(ceilf(localCenter.z)) + vxRadius + 1, 1, points);
	if (minX >= maxX || minY >= maxY || minZ >= maxZ)
	{
		return false;
	}

	GpuBrushParams params{};
	params.brushCenterRadius[0] = center.x;
	params.brushCenterRadius[1] = center.y;
	params.brushCenterRadius[2] = center.z;
	params.brushCenterRadius[3] = settings.radius;
	params.brushParams0[0] = settings.strength;
	params.brushParams0[1] = std::max(0.001f, deltaTime);
	params.brushParams0[2] = settings.hardness;
	params.brushParams0[3] = settings.falloff;
	params.intParams0[0] = static_cast<int32_t>(settings.mode);
	params.intParams0[1] = points;
	params.intParams0[2] = minX;
	params.intParams0[3] = minY;
	params.intParams1[0] = minZ;
	params.intParams1[1] = maxX;
	params.intParams1[2] = maxY;
	params.intParams1[3] = maxZ;
	params.targetHeight = settings.targetHeight;
	params.chunkOriginVoxel[0] = _origin.x;
	params.chunkOriginVoxel[1] = _origin.y;
	params.chunkOriginVoxel[2] = _origin.z;
	params.chunkOriginVoxel[3] = _voxelSize;
	g_gpuBrushConstantBuffer->Write(&params, sizeof(params));

	g_pEnv->_graphicsDevice->SetConstantBufferCS(0, g_gpuBrushConstantBuffer);
	g_pEnv->_graphicsDevice->SetComputeRwTexture3D(0, _gpuDensityTexture);
	g_pEnv->_graphicsDevice->SetComputeShader(g_gpuBrushStage);
	g_pEnv->_graphicsDevice->Dispatch(
		std::max(1u, static_cast<uint32_t>(((maxX - minX) + 7) / 8)),
		std::max(1u, static_cast<uint32_t>(((maxY - minY) + 7) / 8)),
		std::max(1u, static_cast<uint32_t>(((maxZ - minZ) + 7) / 8)));
	g_pEnv->_graphicsDevice->ClearComputeRwTexture3D(0);
	g_pEnv->_graphicsDevice->SetConstantBufferCS(0, nullptr);
	g_pEnv->_graphicsDevice->SetComputeShader(nullptr);

	return true;
}

void VolumetricTerrainChunk::RenderGpuSurface()
{
	if (!_generated || !EnsureGpuSurfacePipeline() || !_gpuSurfaceReady || _gpuSurfaceTriangles == nullptr || _gpuSurfaceDrawArgs == nullptr)
	{
		return;
	}

	GpuSurfaceChunkParams params{};
	params.chunkOriginVoxel[0] = _origin.x;
	params.chunkOriginVoxel[1] = _origin.y;
	params.chunkOriginVoxel[2] = _origin.z;
	params.chunkOriginVoxel[3] = _voxelSize;
	params.worldInfo[0] = static_cast<float>(_params.chunkResolution);
	params.worldInfo[1] = static_cast<float>(_params.chunkWorldSize);
	params.worldInfo[2] = static_cast<float>(_params.chunkWorldSize * _params.chunksY);
	params.worldInfo[3] = 0.0f;
	g_gpuSurfaceChunkBuffer->Write(&params, sizeof(params));

	auto* graphics = g_pEnv->_graphicsDevice;
	graphics->SetTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	graphics->SetInputLayout(nullptr);
	graphics->SetVertexBuffer(0, nullptr);
	graphics->SetVertexShader(g_gpuSurfaceVs);
	graphics->SetGeometryShader(nullptr);
	graphics->SetPixelShader(g_gpuSurfacePs);
	graphics->SetConstantBufferVS(2, graphics->GetEngineConstantBuffer(EngineConstantBuffer::PerFrameBuffer));
	graphics->SetConstantBufferPS(2, graphics->GetEngineConstantBuffer(EngineConstantBuffer::PerFrameBuffer));
	graphics->SetConstantBufferVS(6, g_gpuSurfaceChunkBuffer);
	graphics->SetConstantBufferPS(6, g_gpuSurfaceChunkBuffer);
	graphics->SetVertexStructuredBuffer(0, _gpuSurfaceTriangles);
	graphics->SetBlendState(BlendState::Opaque);
	graphics->SetDepthBufferState(DepthBufferState::DepthDefault);
	graphics->SetCullingMode(CullingMode::NoCulling);
	graphics->DrawInstancedIndirect(_gpuSurfaceDrawArgs);
	graphics->ClearVertexStructuredBuffer(0);
	graphics->SetConstantBufferVS(6, nullptr);
	graphics->SetConstantBufferPS(6, nullptr);
	graphics->SetVertexShader(nullptr);
	graphics->SetPixelShader(nullptr);
	graphics->SetInputLayout(nullptr);
}
}
