#pragma once

#include "../Required.hpp"
#include "../Entity/EntityId.hpp"
#include <unordered_map>
#include <vector>

namespace HexEngine
{
	class Scene;
	class Mesh;
	class Material;

	/**
	 * @brief Compute-shader ray tracing scene (two-level BVH) for GI.
	 *
	 * BLAS: one binned-SAH BVH2 per (Mesh, Material), built on the CPU from the
	 * mesh's resident vertex/index copy and appended into shared GPU buffers.
	 * Each triangle carries its baked linear albedo (the material's albedo
	 * texture at the triangle's UV centroid x GI tint) and emissive, so a hit
	 * needs no material lookup. Triangle winding is normalised so cross(e1,e2)
	 * follows the authored vertex normals - front/back is meaningful to DDGI.
	 *
	 * TLAS: BVH2 over the world AABBs of GI-eligible instances within
	 * r_rtRadius of the camera, rebuilt when the scene's GI geometry revision
	 * changes (movers bump it, throttled), when BLASes finish building, or when
	 * the camera has moved r_rtRadius/8 since the last build.
	 *
	 * Shader side: RtCommon.shader (t20-t23). D3D11 only; a DXR backend would
	 * replace this class behind the same shader-facing contract.
	 */
	class HEX_API RtScene
	{
	public:
		struct GpuNode
		{
			float bmin[3];
			uint32_t leftFirst;
			float bmax[3];
			uint32_t count;
		};
		static_assert(sizeof(GpuNode) == 32, "RtNode mirror");

		struct GpuTri
		{
			float v0[3];
			uint32_t albedo;
			float e1[3];
			uint32_t emisRG;
			float e2[3];
			uint32_t emisBFlags;
		};
		static_assert(sizeof(GpuTri) == 48, "RtTri mirror");

		struct GpuInstance
		{
			float worldToObject[12];
			uint32_t blasNodeOffset;
			uint32_t blasTriOffset;
			uint32_t flags;
			uint32_t pad;
		};
		static_assert(sizeof(GpuInstance) == 64, "RtInstance mirror");

		struct Stats
		{
			uint32_t blasCount = 0;
			uint32_t blasTriangles = 0;
			uint32_t blasNodes = 0;
			uint32_t instances = 0;
			uint32_t pendingBlas = 0;
			uint32_t tlasRebuilds = 0; // cumulative
			float lastTlasBuildMs = 0.0f;
			float lastBlasBuildMs = 0.0f;
			uint64_t gpuBytes = 0;
		};

		~RtScene();

		/** Build/refresh for this frame. Returns true when traceable. */
		bool Update(Scene* scene, const math::Vector3& cameraPosition);
		bool IsReady() const { return _tlasNodeCount > 0 && _instanceCount > 0; }

		/** Bind t20-t23 for a compute / pixel pass. */
		void BindCS(ID3D11DeviceContext* context) const;
		void BindPS(ID3D11DeviceContext* context) const;
		static void UnbindCS(ID3D11DeviceContext* context);
		static void UnbindPS(ID3D11DeviceContext* context);

		void Destroy();
		const Stats& GetStats() const { return _stats; }
		uint64_t GetRevision() const { return _revision; }

	private:
		struct BlasKey
		{
			const Mesh* mesh = nullptr;
			const Material* material = nullptr;
			// StaticMeshComponent::GetUVScale - the vertex shader multiplies
			// texcoords by it, so it changes which texels a triangle samples.
			float uvScaleX = 1.0f;
			float uvScaleY = 1.0f;
			bool operator==(const BlasKey& o) const
			{
				return mesh == o.mesh && material == o.material && uvScaleX == o.uvScaleX && uvScaleY == o.uvScaleY;
			}
		};
		struct BlasKeyHash
		{
			size_t operator()(const BlasKey& k) const
			{
				size_t h = std::hash<const void*>()(k.mesh) ^ (std::hash<const void*>()(k.material) * 0x9e3779b97f4a7c15ull);
				h ^= std::hash<float>()(k.uvScaleX) + 0x7f4a7c15u + (h << 6) + (h >> 2);
				h ^= std::hash<float>()(k.uvScaleY) + 0x9e3779b9u + (h << 6) + (h >> 2);
				return h;
			}
		};
		struct BlasEntry
		{
			uint32_t geometryRevision = 0;
			const void* albedoIdentity = nullptr;
			uint32_t nodeOffset = 0;
			uint32_t nodeCount = 0;
			uint32_t triOffset = 0;
			uint32_t triCount = 0;
			float boundsMin[3] = {};
			float boundsMax[3] = {};
			bool valid = false;
		};
		struct TexturePixels
		{
			const void* identity = nullptr;
			std::vector<uint8_t> pixels;
			bool srgb = false; // _SRGB format: the sampler decodes, so the bake must too
			int32_t width = 0;
			int32_t height = 0;
			bool bgra = false;
			bool valid = false;
		};

		struct LiveInstance
		{
			EntityId entity;
			uint64_t transformVersion = 0;
			const BlasEntry* blas = nullptr;
			float bmin[3] = {};
			float bmax[3] = {};
		};

		bool BuildBlas(const BlasKey& key, BlasEntry& entry);
		// ownerKey (entity index + 1) lands in GpuInstance::pad: a local light
		// skips its own fixture's instance in shadow rays (DDGITrace).
		void FillInstance(const math::Matrix& world, const BlasEntry& blas, GpuInstance& out, float bmin[3], float bmax[3], uint32_t ownerKey) const;
		bool Refit(Scene* scene);
		void UploadTlas();
		const TexturePixels* GetTexturePixels(const Material* material, bool emission);
		bool EnsureBuffer(ID3D11Buffer*& buffer, ID3D11ShaderResourceView*& srv, uint32_t& capacity,
			uint32_t requiredElements, uint32_t stride, bool dynamic, const void* initialData, uint32_t initialCount);
		void UploadBlasData();

		std::unordered_map<BlasKey, BlasEntry, BlasKeyHash> _blas;
		std::unordered_map<const void*, TexturePixels> _textures;
		std::vector<GpuNode> _blasNodes;
		std::vector<GpuTri> _tris;
		uint32_t _uploadedNodes = 0;
		uint32_t _uploadedTris = 0;

		ID3D11Buffer* _blasNodeBuffer = nullptr;
		ID3D11ShaderResourceView* _blasNodeSrv = nullptr;
		uint32_t _blasNodeCapacity = 0;
		ID3D11Buffer* _triBuffer = nullptr;
		ID3D11ShaderResourceView* _triSrv = nullptr;
		uint32_t _triCapacity = 0;
		ID3D11Buffer* _tlasBuffer = nullptr;
		ID3D11ShaderResourceView* _tlasSrv = nullptr;
		uint32_t _tlasCapacity = 0;
		ID3D11Buffer* _instanceBuffer = nullptr;
		ID3D11ShaderResourceView* _instanceSrv = nullptr;
		uint32_t _instanceCapacity = 0;

		std::vector<LiveInstance> _live;        // leaf order (matches _instancesCpu)
		std::vector<GpuInstance> _instancesCpu;
		std::vector<GpuNode> _tlasCpu;
		std::chrono::steady_clock::time_point _lastFullBuild = {};
		uint32_t _tlasNodeCount = 0;
		uint32_t _instanceCount = 0;
		uint64_t _lastGeometryRevision = ~0ull;
		math::Vector3 _lastBuildCenter = math::Vector3(1e30f, 1e30f, 1e30f);
		bool _tlasDirty = true;
		uint64_t _revision = 0;
		Stats _stats;
	};
}
