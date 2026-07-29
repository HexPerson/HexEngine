#pragma once

#include "../Required.hpp"
#include "IShader.hpp"

struct ID3D11Buffer;
struct ID3D11ShaderResourceView;
struct ID3D11UnorderedAccessView;

namespace HexEngine
{
	class Scene;
	class Camera;
	class ITexture2D;

	// Phase 2, first slice: clustered light culling.
	//
	// Gathers EVERY point and spot light in the scene (not the closest-16) into
	// a GPU buffer and bins them into a 16 x 9 x 32 view-frustum cluster grid in
	// compute. The depth slicing reuses the froxel volumetric system's
	// exponential mapping so a later step can share assignment between surface
	// lighting and fog. Nothing consumes the lists yet - the deliverable of
	// this slice is the lists themselves plus the occupancy heatmap
	// (r_clusterDebug) that proves the binning against the world.
	//
	// Raw D3D11 by deliberate convention: every compute producer in this
	// codebase (DiffuseGI, VolumetricScattering, GpuVisibilityCulling) drops to
	// the native context, and the D3D12 story for all of them is the same
	// Phase B5 port.
	class HEX_API ClusteredLighting
	{
	public:
		static constexpr uint32_t kClustersX = 16;
		static constexpr uint32_t kClustersY = 9;
		static constexpr uint32_t kClustersZ = 32;
		static constexpr uint32_t kClusterCount = kClustersX * kClustersY * kClustersZ;
		static constexpr uint32_t kMaxLightsPerCluster = 64;
		static constexpr uint32_t kMaxLights = 1024;

		bool Create();
		void Destroy();

		// Gather lights, upload, dispatch the cull. Main camera only.
		void UpdateAndCull(Scene* scene, Camera* camera);

		// Render the occupancy heatmap into the debug texture. Needs the
		// gbuffer normal RT (its .w carries view depth).
		void RenderDebug(ITexture2D* gbufferNormalDepth);

		ITexture2D* GetDebugTexture() const { return _debugTexture; }
		uint32_t GetLastLightCount() const { return _lastLightCount; }

	private:
		struct GpuLight
		{
			math::Vector4 posRadius;
			math::Vector4 colorStrength;
			math::Vector4 dirCone;
			math::Vector4 params;
		};

		struct ClusterConstants
		{
			math::Matrix view;
			math::Vector4 screenParams; // x tanHalfFovX, y tanHalfFovY, z lightCount, w unused
		};

		std::shared_ptr<IShader> _cullShader;
		std::shared_ptr<IShader> _debugShader;

		ID3D11Buffer* _lightsBuffer = nullptr;
		ID3D11ShaderResourceView* _lightsSrv = nullptr;
		ID3D11Buffer* _countsBuffer = nullptr;
		ID3D11UnorderedAccessView* _countsUav = nullptr;
		ID3D11ShaderResourceView* _countsSrv = nullptr;
		ID3D11Buffer* _listsBuffer = nullptr;
		ID3D11UnorderedAccessView* _listsUav = nullptr;
		ID3D11Buffer* _constantsBuffer = nullptr;

		ITexture2D* _debugTexture = nullptr;
		ID3D11UnorderedAccessView* _debugUav = nullptr;

		// SRV over the gbuffer normal RT, cached against the texture it was
		// created for (the RT is recreated on resize).
		ID3D11ShaderResourceView* _normalDepthSrv = nullptr;
		void* _normalDepthSrvSource = nullptr;

		uint32_t _lastLightCount = 0;
	};
}
