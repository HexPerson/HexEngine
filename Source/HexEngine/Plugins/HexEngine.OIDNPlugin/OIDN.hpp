#pragma once

#include <HexEngine.Core/HexEngine.hpp>
#include <OpenImageDenoise/oidn.hpp>

class OIDN : public HexEngine::IDenoiserProvider
{
public:
	virtual bool Create() override;

	virtual void Destroy() override;

	virtual void CreateBuffers(int32_t width, int32_t height, HexEngine::ITexture2D* diffuseSignalInput, HexEngine::ITexture2D* diffuseHitDistance, HexEngine::ITexture2D* specularSignalInput, HexEngine::ITexture2D* specularHitDistance, HexEngine::ITexture2D* normalAndDepth, HexEngine::ITexture2D* material, HexEngine::ITexture2D* motionVectors) override;

	virtual void BuildFrameData(HexEngine::DenoiserFrameData& fd, HexEngine::ITexture2D* diffuseSignalInput, HexEngine::ITexture2D* diffuseHitDistance, HexEngine::ITexture2D* specularSignalInput, HexEngine::ITexture2D* specularHitDistance, HexEngine::ITexture2D* normalAndDepth, HexEngine::ITexture2D* material, HexEngine::ITexture2D* motionVectors) override;

	virtual void FilterFrame(const HexEngine::DenoiserFrameData& fd, HexEngine::ITexture2D* output) override;

private:
	oidn::DeviceRef _device;
	oidn::BufferRef _colourBuf;
	oidn::BufferRef _albedoBuf;
	oidn::BufferRef _normalBuf;
	oidn::BufferRef _outputBuf;
	oidn::FilterRef _filter;

	HexEngine::ITexture2D* _beautyStaging = nullptr;
	HexEngine::ITexture2D* _colourStaging = nullptr;
	HexEngine::ITexture2D* _normalStaging = nullptr;
	HexEngine::ITexture2D* _outputStaging = nullptr;
};

