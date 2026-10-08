#pragma once

// D3D11 backend for AMD FidelityFX FSR 2.2 (FidelityFX-FSR2 v2.2.1).
//
// AMD ships DX12 and Vulkan backends only. This one implements the same
// FfxFsr2Interface callbacks over D3D11:
//   - pass shaders are compiled at plugin build time with fxc (cs_5_0, see
//     CompileShaders.cmd) into embedded blobs, and their bindings are recovered
//     with D3DReflect - the same name/slot data the DX12 backend gets from
//     FidelityFX_SC's generated headers;
//   - internal resources are plain D3D11 textures with an SRV plus one UAV per mip;
//   - jobs run on the immediate context with the CS state saved and restored, so
//     the engine's cached device state is undisturbed.
//
// The pass permutation is FIXED at build time (HDR colour input, render-resolution
// non-jittered motion vectors, standard depth, LUT Lanczos). The backend refuses
// context flags that would need a different permutation.

#include <ffx_fsr2.h>

struct ID3D11Device;
struct ID3D11Resource;

// Context flags the embedded shaders were compiled for (and must be passed).
#define FFX_FSR2_DX11_REQUIRED_FLAGS (FFX_FSR2_ENABLE_HIGH_DYNAMIC_RANGE)
// Flags that would select a different permutation (must NOT be passed).
#define FFX_FSR2_DX11_FORBIDDEN_FLAGS (FFX_FSR2_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS | \
	FFX_FSR2_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION | FFX_FSR2_ENABLE_DEPTH_INVERTED | FFX_FSR2_ENABLE_TEXTURE1D_USAGE)

size_t ffxFsr2GetScratchMemorySizeDX11();

FfxErrorCode ffxFsr2GetInterfaceDX11(FfxFsr2Interface* outInterface, ID3D11Device* device,
	void* scratchBuffer, size_t scratchBufferSize);

// Wraps an engine texture for FfxFsr2DispatchDescription. Null in, null resource out.
FfxResource ffxGetResourceDX11(ID3D11Resource* resource, const wchar_t* name);

// Optional sink for backend diagnostics (pipeline/resource creation failures).
typedef void (*FfxFsr2Dx11LogFunc)(const char* message);
void ffxFsr2SetLogCallbackDX11(FfxFsr2Dx11LogFunc callback);
