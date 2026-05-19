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
	bool CreateDevice();
	void ReleaseResources();
	bool RecreateResources(int32_t width, int32_t height, HexEngine::ITexture2D* specularSignalInput, HexEngine::ITexture2D* output);
	bool CreateSharedTextureClone(HexEngine::ITexture2D* source, HexEngine::ITexture2D*& destination, const char* debugName);
	bool CreateInteropBuffer(oidn::BufferRef& buffer, HexEngine::ITexture2D* texture, oidn::Format format, size_t byteSize);
	bool DescribeTextureFormat(HexEngine::ITexture2D* texture, oidn::Format& format, size_t& pixelStride, size_t& rowStride) const;
	bool CopyTexture(HexEngine::ITexture2D* source, HexEngine::ITexture2D* destination) const;
	void LogLastError(const char* operation);

private:
	bool _created = false;
	bool _useGpuInterop = false;
	int32_t _width = 0;
	int32_t _height = 0;
	oidn::DeviceRef _device;
	oidn::BufferRef _colourBuf;
	oidn::BufferRef _outputBuf;
	oidn::FilterRef _filter;
	HexEngine::ITexture2D* _sharedColour = nullptr;
	HexEngine::ITexture2D* _sharedOutput = nullptr;
	oidn::Format _colourFormat = oidn::Format::Half3;
	oidn::Format _outputFormat = oidn::Format::Half3;
	size_t _colourPixelStride = 0;
	size_t _colourRowStride = 0;
	size_t _outputPixelStride = 0;
	size_t _outputRowStride = 0;
};

