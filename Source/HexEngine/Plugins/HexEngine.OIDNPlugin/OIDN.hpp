
#pragma once

#include <HexEngine.Core/HexEngine.hpp>
#include <OpenImageDenoise/oidn.hpp>

class OIDN : public IDenoiserProvider
{
public:
	virtual bool Create() override;

	virtual void Destroy() override;

	virtual void CreateBuffers(int32_t width, int32_t height, ITexture2D* beauty, ITexture2D* normals, ITexture2D* albedo) override;

	virtual void BuildFrameData(DenoiserFrameData& fd, ITexture2D* beauty, ITexture2D* normals, ITexture2D* albedo) override;

	virtual void FilterFrame(const DenoiserFrameData& fd, ITexture2D* beauty) override;

private:
	oidn::DeviceRef _device;
	oidn::BufferRef _colourBuf;
	oidn::BufferRef _albedoBuf;
	oidn::BufferRef _normalBuf;
	oidn::BufferRef _outputBuf;
	oidn::FilterRef _filter;

	ITexture2D* _beautyStaging = nullptr;
	ITexture2D* _colourStaging = nullptr;
	ITexture2D* _normalStaging = nullptr;
	ITexture2D* _outputStaging = nullptr;
};
