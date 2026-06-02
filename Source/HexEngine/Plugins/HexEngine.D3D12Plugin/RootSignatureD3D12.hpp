
#pragma once

#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>

/**
 * @brief HexEngine's universal D3D12 root signature.
 *
 * Built once at device init and bound for every draw. Three descriptor tables
 * (CBV, SRV, UAV) plus a static-sampler set. Sized to cover HexEngine's
 * widest usage today; if a shader binds outside these ranges D3D12 will
 * validate-error and we bump the range count.
 *
 * Layout (visible to all shader stages so the same root sig works for both
 * graphics and compute):
 *   Param 0: CBV table   b0..b15     (16 CBVs)
 *   Param 1: SRV table   t0..t63     (64 SRVs - covers material textures +
 *                                     GBuffer + shadows + GI volumes)
 *   Param 2: UAV table   u0..u15     (16 UAVs)
 *
 * Static samplers (s0..s4) match the standard sampler order the D3D11 plugin
 * binds in BeginFrame:
 *   s0 - anisotropic wrap
 *   s1 - linear comparison clamp (shadow PCF)
 *   s2 - point wrap
 *   s3 - linear mirrored
 *   s4 - linear wrap
 */
class RootSignatureD3D12
{
public:
	bool Create(ID3D12Device* device);
	void Destroy() { _rootSig.Reset(); }

	ID3D12RootSignature* Get() const { return _rootSig.Get(); }

	static constexpr uint32_t kCbvRootParam = 0;
	static constexpr uint32_t kSrvRootParam = 1;
	static constexpr uint32_t kUavRootParam = 2;

	static constexpr uint32_t kCbvCount = 16;
	static constexpr uint32_t kSrvCount = 64;
	static constexpr uint32_t kUavCount = 16;

private:
	Microsoft::WRL::ComPtr<ID3D12RootSignature> _rootSig;
};
