#pragma once

#include "../Required.hpp"
#include "PVS.hpp"
#include "../Entity/Component/StaticMeshComponent.hpp"
#include <functional>

namespace HexEngine
{
	class Camera;
	class ITexture2D;

	struct GpuCullingStats
	{
		uint32_t totalCandidates = 0;
		uint32_t frustumRejected = 0;
		uint32_t occlusionRejected = 0;
		uint32_t visibleInstances = 0;
		uint32_t submittedDraws = 0;
		float cpuBuildMs = 0.0f;
		float gpuFrustumMs = 0.0f;
		float gpuOcclusionMs = 0.0f;
		bool usedOcclusion = false;
		bool usedDepthFallback = false;
	};

	class HEX_API GpuVisibilityCulling
	{
	public:
		void Create();
		void Destroy();
		void Resize(uint32_t width, uint32_t height);

		void BeginFrame(uint64_t frameIndex, Camera* camera);
		void BuildDepthPyramid(ITexture2D* depthSource);
		void MarkDepthFallbackUsed() { _stats.usedDepthFallback = true; }

		// Returns true when a renderable is already rejected by the CPU fine
		// cull (frustum); such entries are never sent to the GPU, which then
		// does occlusion only.
		using CpuCullPredicate = std::function<bool(const RenderableSnapshot&)>;

		bool CullOpaqueRenderables(
			RenderBatchSnapshot& snapshot,
			Camera* camera,
			LayerMask layerMask,
			MeshRenderFlags renderFlags,
			const CpuCullPredicate* cpuCulled = nullptr);

		// The snapshot was rebuilt: every stableIndex changed meaning, so
		// in-flight readback results and per-entry state are invalidated.
		void NotifySnapshotRebuilt();

		void ReportSubmission(uint32_t submittedDraws, uint32_t visibleInstances);
		const GpuCullingStats& GetStats() const { return _stats; }
		bool HasUsableHistory() const { return _hzbHistoryValid; }

		void DebugDraw(const RenderBatchSnapshot& snapshot);

	private:
		struct GpuCullCandidate
		{
			// Layout mirrored by GpuFrustumCull/GpuOcclusionCull.shader.
			math::Vector4 sphereWs;
			math::Vector4 obbCenter;
			math::Vector4 obbExtents;
			math::Vector4 obbOrientation;
			uint32_t stableIndex = 0;
			uint32_t entityKeyLo = 0;
			uint32_t entityKeyHi = 0;
			uint32_t flags = 0;
		};
		static_assert(sizeof(GpuCullCandidate) == 80, "GpuCullCandidate must match the cull shaders' struct");

		struct GpuCullConstants
		{
			math::Matrix view;
			math::Matrix projection;
			math::Matrix viewProjection;
			math::Vector4 frustumPlanes[6];
			math::Vector4 cameraPos;
			math::Vector4 viewportSizeInvSize;
			math::Vector4 hzbInfo;
			math::Vector4 cullParams0;
			math::Vector4 cullParams1;
			// The camera the HZB was rendered from (GpuOcclusionCull.shader).
			math::Matrix hzbViewProjection;
			// x,y = A,B of ndcZ = A + B/viewZ for that camera; z = relative bias.
			math::Vector4 hzbDepthParams;
		};

		enum CandidateFlags : uint32_t
		{
			CandidateForceVisible = HEX_BITSET(0),
			CandidateOcclusionEligible = HEX_BITSET(1)
		};

		struct HzbResources
		{
			ID3D11Texture2D* texture = nullptr;
			ID3D11ShaderResourceView* srv = nullptr;
			std::vector<ID3D11ShaderResourceView*> mipSrvs;
			std::vector<ID3D11UnorderedAccessView*> mipUavs;
		};

		void DestroyBuffers();
		void DestroyHzbResources(HzbResources& resources);
		void EnsureCandidateCapacity(uint32_t candidateCount);
		void EnsureHzb(uint32_t width, uint32_t height);
		void EnsurePerEntryState(uint32_t entryCount);
		// Returns the total snapshot entry count (stableIndex range).
		uint32_t GatherCandidates(
			RenderBatchSnapshot& snapshot,
			std::vector<GpuCullCandidate>& outCandidates,
			std::vector<RenderableSnapshot*>& outRenderableMap,
			const math::Vector3& cameraPos,
			LayerMask layerMask,
			const CpuCullPredicate* cpuCulled);
		void BuildFrustumPlanes(const math::Matrix& viewProjection, math::Vector4 outPlanes[6]) const;
		void DispatchFrustumPass(ID3D11DeviceContext* context, uint32_t candidateCount);
		void DispatchOcclusionPass(ID3D11DeviceContext* context, uint32_t candidateCount, bool useOcclusion);
		bool ReadbackVisibility(const std::vector<GpuCullCandidate>& candidates, uint32_t& outResultCount);
		uint64_t MakeEntityKey(const RenderableSnapshot& snapshot) const;
		bool ShouldBypassOcclusion(const RenderableSnapshot& snapshot, const math::Vector3& cameraPos) const;
		bool IsTransparentMaterial(const Material* material) const;

	private:
		std::shared_ptr<IShader> _frustumCullShader;
		std::shared_ptr<IShader> _occlusionCullShader;
		std::shared_ptr<IShader> _buildDepthPyramidShader;
		IConstantBuffer* _cullConstantBuffer = nullptr;

		ID3D11Buffer* _candidateBuffer = nullptr;
		ID3D11ShaderResourceView* _candidateSrv = nullptr;
		ID3D11Buffer* _frustumVisibilityBuffer = nullptr;
		ID3D11ShaderResourceView* _frustumVisibilitySrv = nullptr;
		ID3D11UnorderedAccessView* _frustumVisibilityUav = nullptr;
		ID3D11Buffer* _finalVisibilityBuffer = nullptr;
		ID3D11UnorderedAccessView* _finalVisibilityUav = nullptr;

		static constexpr uint32_t ReadbackLatencyFrames = 3;
		ID3D11Buffer* _visibilityReadback[ReadbackLatencyFrames] = {};
		uint32_t _visibilityReadbackCount[ReadbackLatencyFrames] = {};
		bool _visibilityReadbackReady[ReadbackLatencyFrames] = {};
		std::vector<uint32_t> _visibilityReadbackStableIndex[ReadbackLatencyFrames];
		uint64_t _visibilityReadbackGeneration[ReadbackLatencyFrames] = {};
		uint32_t _candidateCapacity = 0;
		uint32_t _lastDispatchCandidateCount = 0;

		// Per-snapshot-entry state indexed by stableIndex (flat arrays; this
		// replaced four unordered_maps keyed by entity that cost ~125k hash
		// ops + 25k allocating inserts per frame at 25k candidates).
		struct PerEntryState
		{
			uint64_t resultFrame = 0;   // frame the last GPU result for this entry was read
			uint8_t resultBits = 0x3;   // bit0 frustum, bit1 final
			uint8_t rejectStreak = 0;
			uint8_t graceRemaining = 0;
			uint8_t frozenVisible = 1;
		};
		std::vector<PerEntryState> _perEntry;
		uint64_t _perEntryGeneration = 0;
		uint64_t _snapshotGeneration = 1;
		uint32_t _lastSnapshotEntryCount = 0;

		// Scratch reused across frames (no per-frame allocation).
		std::vector<GpuCullCandidate> _scratchCandidates;
		std::vector<RenderableSnapshot*> _scratchRenderableMap;

		HzbResources _hzbRead;
		HzbResources _hzbWrite;
		uint32_t _hzbWidth = 0;
		uint32_t _hzbHeight = 0;
		uint32_t _hzbMipCount = 0;
		bool _hzbHistoryValid = false;
		// What the HZB depth was rendered with: occlusion tests project bounds
		// through THIS, and only run for the camera that owns it (another
		// camera's depth says nothing about what this one can see).
		math::Matrix _hzbViewProjection = math::Matrix::Identity;
		math::Vector2 _hzbDepthAB = math::Vector2(1.0f, 0.0f);
		const Camera* _hzbCamera = nullptr;
		// Set by BeginFrame, latched into the two above by BuildDepthPyramid.
		math::Matrix _frameViewProjection = math::Matrix::Identity;
		math::Vector2 _frameDepthAB = math::Vector2(1.0f, 0.0f);
		const Camera* _frameCamera = nullptr;

		uint64_t _frameIndex = 0;
		math::Vector3 _lastCameraPos = math::Vector3::Zero;
		math::Vector3 _lastCameraLookDir = math::Vector3::Forward;
		bool _cameraPosValid = false;
		bool _cameraLookValid = false;
		bool _cameraMovedFastThisFrame = false;
		bool _cameraRotatedFastThisFrame = false;
		uint32_t _cameraStableFrames = 0;

		GpuCullingStats _stats;
	};
}
