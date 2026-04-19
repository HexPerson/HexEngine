#pragma once

#include "../Required.hpp"
#include "PVS.hpp"
#include "../Entity/Component/StaticMeshComponent.hpp"
#include <unordered_map>

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

		bool CullOpaqueRenderables(
			RenderBatchSnapshot& snapshot,
			Camera* camera,
			LayerMask layerMask,
			MeshRenderFlags renderFlags);

		void ReportSubmission(uint32_t submittedDraws, uint32_t visibleInstances);
		const GpuCullingStats& GetStats() const { return _stats; }
		bool HasUsableHistory() const { return _hzbHistoryValid; }

		void DebugDraw(const RenderBatchSnapshot& snapshot);

	private:
		struct GpuCullCandidate
		{
			math::Vector4 sphereWs;
			math::Vector4 occlusionCenterExtent;
			uint32_t stableIndex = 0;
			uint32_t entityKeyLo = 0;
			uint32_t entityKeyHi = 0;
			uint32_t flags = 0;
		};

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
		void ClearSnapshotFlags(RenderBatchSnapshot& snapshot);
		bool GatherCandidates(
			RenderBatchSnapshot& snapshot,
			std::vector<GpuCullCandidate>& outCandidates,
			std::vector<RenderableSnapshot*>& outRenderableMap,
			const math::Vector3& cameraPos,
			LayerMask layerMask);
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
		std::vector<uint64_t> _visibilityReadbackKeys[ReadbackLatencyFrames];
		uint32_t _candidateCapacity = 0;
		uint32_t _lastDispatchCandidateCount = 0;

		std::vector<uint32_t> _cpuVisibility;
		std::unordered_map<uint64_t, uint32_t> _cpuVisibilityByEntity;
		std::unordered_map<uint64_t, bool> _frozenVisibility;
		std::unordered_map<uint64_t, uint32_t> _graceFramesRemaining;
		std::unordered_map<uint64_t, uint32_t> _occlusionRejectStreak;

		HzbResources _hzbRead;
		HzbResources _hzbWrite;
		uint32_t _hzbWidth = 0;
		uint32_t _hzbHeight = 0;
		uint32_t _hzbMipCount = 0;
		bool _hzbHistoryValid = false;

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
