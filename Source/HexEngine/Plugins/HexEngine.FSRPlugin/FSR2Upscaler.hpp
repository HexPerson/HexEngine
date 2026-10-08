#pragma once

#include <HexEngine.Core\HexEngine.hpp>
#include <HexEngine.Core\Graphics\IUpscalerProvider.hpp>

struct FfxFsr2Context;

// AMD FidelityFX Super Resolution 2.2 behind IUpscalerProvider, on the plugin's
// own D3D11 backend (ffx_fsr2_dx11.cpp).
class FSR2Upscaler : public HexEngine::IUpscalerProvider
{
public:
	~FSR2Upscaler();

	virtual bool Create() override;
	virtual void Destroy() override;

	virtual const char* GetName() const override { return "FSR 2.2"; }
	virtual bool IsSupported() override;
	virtual bool GetRenderResolution(HexEngine::UpscalerQuality quality, uint32_t displayWidth, uint32_t displayHeight,
		uint32_t& renderWidth, uint32_t& renderHeight) override;
	virtual math::Vector2 GetJitterOffset(uint64_t frameIndex, uint32_t renderWidth, uint32_t displayWidth) override;
	virtual bool Evaluate(const HexEngine::UpscalerFrameInputs& inputs) override;

private:
	bool EnsureContext(uint32_t displayWidth, uint32_t displayHeight, uint32_t renderWidth, uint32_t renderHeight);
	void DestroyContext();

	FfxFsr2Context* _context = nullptr;
	void* _scratch = nullptr;
	uint32_t _displayWidth = 0;
	uint32_t _displayHeight = 0;
	uint32_t _maxRenderWidth = 0;
	uint32_t _maxRenderHeight = 0;

	int32_t _supported = -1;	// -1 unknown, 0 no, 1 yes
	bool _failed = false;		// context creation failed for the current sizes; don't retry every frame
	bool _resetNext = true;		// a new context has no history
};
