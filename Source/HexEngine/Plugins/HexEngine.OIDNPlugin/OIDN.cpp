#include "OIDN.hpp"

#include <d3d11.h>

namespace
{
	void ErrorFunc(void* userPtr, oidn::Error code, const char* message)
	{
		(void)userPtr;
		if (code != oidn::Error::None && message != nullptr)
			LOG_CRIT("OIDN error: %s", message);
	}

	bool HasExternalMemorySupport(oidn::ExternalMemoryTypeFlags flags)
	{
		return static_cast<bool>(flags & oidn::ExternalMemoryTypeFlag::D3D11TextureKMT) ||
			static_cast<bool>(flags & oidn::ExternalMemoryTypeFlag::D3D11ResourceKMT);
	}

	oidn::ExternalMemoryTypeFlag GetPreferredExternalMemoryType(oidn::ExternalMemoryTypeFlags flags)
	{
		if (static_cast<bool>(flags & oidn::ExternalMemoryTypeFlag::D3D11TextureKMT))
			return oidn::ExternalMemoryTypeFlag::D3D11TextureKMT;

		return oidn::ExternalMemoryTypeFlag::D3D11ResourceKMT;
	}
}

bool OIDN::Create()
{
	if (_created)
		return true;

	// Backend gate: GPU interop in this plugin shares D3D11 textures with OIDN.
	// Refuse to come up under any non-D3D11 backend - the resource sharing path
	// would otherwise reinterpret_cast a non-D3D11 device handle from
	// GetNativeDevice() and crash.
	if (HexEngine::g_pEnv != nullptr && HexEngine::g_pEnv->_graphicsDevice != nullptr &&
		HexEngine::g_pEnv->_graphicsDevice->GetBackend() != HexEngine::GraphicsBackend::D3D11)
	{
		LOG_WARN("HexEngine.OIDNPlugin: disabled (active backend is not D3D11; per-backend port pending)");
		return false;
	}

	_created = CreateDevice();
	return _created;
}

bool OIDN::CreateDevice()
{
	oidn::DeviceRef selectedDevice;
	bool usingGpuInterop = false;

	for (int32_t i = 0; i < oidnGetNumPhysicalDevices(); ++i)
	{
		const auto deviceType = static_cast<oidn::DeviceType>(oidnGetPhysicalDeviceInt(i, "type"));
		if (deviceType == oidn::DeviceType::CPU)
			continue;

		size_t uuidSize = OIDN_UUID_SIZE;
		oidn::UUID uuid = {};
		const void* uuidData = oidnGetPhysicalDeviceData(i, "uuid", &uuidSize);
		if (uuidData == nullptr || uuidSize != OIDN_UUID_SIZE)
			continue;

		memcpy(&uuid, uuidData, uuidSize);

		oidn::DeviceRef candidate = oidn::newDevice(uuid);
		if (!candidate)
			continue;

		candidate.setErrorFunction(ErrorFunc, nullptr);
		candidate.commit();

		const auto externalMemoryTypes = candidate.get<oidn::ExternalMemoryTypeFlags>("externalMemoryTypes");
		if (!HasExternalMemorySupport(externalMemoryTypes))
		{
			const char* deviceName = oidnGetPhysicalDeviceString(i, "name");
			LOG_WARN("OIDN: skipping device '%s' because it does not expose D3D11 interop", deviceName ? deviceName : "unknown");
			continue;
		}

		selectedDevice = candidate;
		usingGpuInterop = true;

		const char* deviceName = oidnGetPhysicalDeviceString(i, "name");
		LOG_DEBUG("OIDN: selected accelerated device '%s'", deviceName ? deviceName : "unknown");
		break;
	}

	if (!selectedDevice)
	{
		selectedDevice = oidn::newDevice();
		if (!selectedDevice)
			return false;

		selectedDevice.setErrorFunction(ErrorFunc, nullptr);
		selectedDevice.commit();
		LOG_WARN("OIDN: falling back to default device without D3D11 interop");
	}

	_device = selectedDevice;
	_useGpuInterop = usingGpuInterop;
	return true;
}

void OIDN::Destroy()
{
	ReleaseResources();
	_created = false;
	_width = 0;
	_height = 0;
	_useGpuInterop = false;

	if (_device)
		_device.release();
}

void OIDN::ReleaseResources()
{
	if (_filter)
		_filter.release();

	if (_colourBuf)
		_colourBuf.release();

	if (_outputBuf)
		_outputBuf.release();

	SAFE_DELETE(_sharedColour);
	SAFE_DELETE(_sharedOutput);
}

void OIDN::CreateBuffers(int32_t width, int32_t height, HexEngine::ITexture2D* diffuseSignalInput, HexEngine::ITexture2D* diffuseHitDistance, HexEngine::ITexture2D* specularSignalInput, HexEngine::ITexture2D* specularHitDistance, HexEngine::ITexture2D* normalAndDepth, HexEngine::ITexture2D* material, HexEngine::ITexture2D* motionVectors)
{
	(void)diffuseSignalInput;
	(void)diffuseHitDistance;
	(void)specularHitDistance;
	(void)material;
	(void)motionVectors;

	if (!_created && !Create())
		return;

	RecreateResources(width, height, specularSignalInput, specularSignalInput);
}

bool OIDN::RecreateResources(int32_t width, int32_t height, HexEngine::ITexture2D* specularSignalInput, HexEngine::ITexture2D* output)
{
	if (!_device || !specularSignalInput || !output)
		return false;

	oidn::Format colourFormat = oidn::Format::Half3;
	oidn::Format outputFormat = oidn::Format::Half3;
	size_t colourPixelStride = 0;
	size_t colourRowStride = 0;
	size_t outputPixelStride = 0;
	size_t outputRowStride = 0;

	if (!DescribeTextureFormat(specularSignalInput, colourFormat, colourPixelStride, colourRowStride) ||
		!DescribeTextureFormat(output, outputFormat, outputPixelStride, outputRowStride))
	{
		return false;
	}

	const bool sameSize = _width == width && _height == height;
	const bool sameFormats =
		_colourFormat == colourFormat &&
		_outputFormat == outputFormat &&
		_colourPixelStride == colourPixelStride &&
		_outputPixelStride == outputPixelStride;

	if (sameSize && sameFormats && _filter && _sharedColour && _sharedOutput)
		return true;

	ReleaseResources();

	_width = width;
	_height = height;
	_colourFormat = colourFormat;
	_outputFormat = outputFormat;
	_colourPixelStride = colourPixelStride;
	_colourRowStride = colourRowStride;
	_outputPixelStride = outputPixelStride;
	_outputRowStride = outputRowStride;

	if (!_useGpuInterop)
	{
		LOG_WARN("OIDN: D3D11 interop is unavailable on the selected device");
		return false;
	}

	if (!CreateSharedTextureClone(specularSignalInput, _sharedColour, "OIDN_SharedColour") ||
		!CreateSharedTextureClone(output, _sharedOutput, "OIDN_SharedOutput"))
	{
		ReleaseResources();
		return false;
	}

	const size_t colourByteSize = _colourRowStride * static_cast<size_t>(_height);
	const size_t outputByteSize = _outputRowStride * static_cast<size_t>(_height);

	if (!CreateInteropBuffer(_colourBuf, _sharedColour, _colourFormat, colourByteSize) ||
		!CreateInteropBuffer(_outputBuf, _sharedOutput, _outputFormat, outputByteSize))
	{
		ReleaseResources();
		return false;
	}

	_filter = _device.newFilter("RT");
	_filter.setImage("color", _colourBuf, _colourFormat, _width, _height, 0, _colourPixelStride, _colourRowStride);
	_filter.setImage("output", _outputBuf, _outputFormat, _width, _height, 0, _outputPixelStride, _outputRowStride);
	_filter.set("hdr", true);
	_filter.set("quality", oidn::Quality::Balanced);
	_filter.commit();

	LogLastError("commit");
	return static_cast<bool>(_filter);
}

bool OIDN::CreateSharedTextureClone(HexEngine::ITexture2D* source, HexEngine::ITexture2D*& destination, const char* debugName)
{
	if (!source)
		return false;

	auto* nativeSource = reinterpret_cast<ID3D11Texture2D*>(source->GetNativePtr());
	if (!nativeSource)
		return false;

	D3D11_TEXTURE2D_DESC desc = {};
	nativeSource->GetDesc(&desc);

	if (desc.SampleDesc.Count != 1)
	{
		LOG_WARN("OIDN: multisampled textures are not supported by the current real-time interop path");
		return false;
	}

	destination = HexEngine::g_pEnv->_graphicsDevice->CreateTexture2D(
		static_cast<int32_t>(desc.Width),
		static_cast<int32_t>(desc.Height),
		desc.Format,
		static_cast<int32_t>(desc.ArraySize),
		desc.BindFlags,
		static_cast<int32_t>(desc.MipLevels),
		static_cast<int32_t>(desc.SampleDesc.Count),
		static_cast<int32_t>(desc.SampleDesc.Quality),
		nullptr,
		static_cast<D3D11_CPU_ACCESS_FLAG>(desc.CPUAccessFlags),
		(desc.BindFlags & D3D11_BIND_RENDER_TARGET) ? D3D11_RTV_DIMENSION_TEXTURE2D : D3D11_RTV_DIMENSION_UNKNOWN,
		D3D11_UAV_DIMENSION_UNKNOWN,
		(desc.BindFlags & D3D11_BIND_SHADER_RESOURCE) ? D3D11_SRV_DIMENSION_TEXTURE2D : D3D11_SRV_DIMENSION_UNKNOWN,
		D3D11_DSV_DIMENSION_UNKNOWN,
		desc.Usage,
		desc.MiscFlags | D3D11_RESOURCE_MISC_SHARED);

	if (!destination)
		return false;

#ifdef _DEBUG
	destination->SetDebugName(debugName);
#endif
	return true;
}

bool OIDN::CreateInteropBuffer(oidn::BufferRef& buffer, HexEngine::ITexture2D* texture, oidn::Format format, size_t byteSize)
{
	(void)format;

	const auto externalMemoryTypes = _device.get<oidn::ExternalMemoryTypeFlags>("externalMemoryTypes");
	if (!HasExternalMemorySupport(externalMemoryTypes))
		return false;

	buffer = _device.newBuffer(
		GetPreferredExternalMemoryType(externalMemoryTypes),
		texture->GetSharedHandle(),
		nullptr,
		byteSize);

	LogLastError("import buffer");
	return static_cast<bool>(buffer);
}

bool OIDN::DescribeTextureFormat(HexEngine::ITexture2D* texture, oidn::Format& format, size_t& pixelStride, size_t& rowStride) const
{
	if (!texture)
		return false;

	switch (static_cast<DXGI_FORMAT>(texture->GetFormat()))
	{
	case DXGI_FORMAT_R16G16B16A16_FLOAT:
		format = oidn::Format::Half3;
		pixelStride = sizeof(uint16_t) * 4;
		rowStride = static_cast<size_t>(texture->GetWidth()) * pixelStride;
		return true;
	case DXGI_FORMAT_R32G32B32A32_FLOAT:
		format = oidn::Format::Float3;
		pixelStride = sizeof(float) * 4;
		rowStride = static_cast<size_t>(texture->GetWidth()) * pixelStride;
		return true;
	default:
		LOG_WARN("OIDN: unsupported texture format %u for hardware interop", texture->GetFormat());
		return false;
	}
}

bool OIDN::CopyTexture(HexEngine::ITexture2D* source, HexEngine::ITexture2D* destination) const
{
	if (!source || !destination)
		return false;

	auto* deviceContext = reinterpret_cast<ID3D11DeviceContext*>(HexEngine::g_pEnv->_graphicsDevice->GetNativeDeviceContext());
	auto* sourceTexture = reinterpret_cast<ID3D11Texture2D*>(source->GetNativePtr());
	auto* destinationTexture = reinterpret_cast<ID3D11Texture2D*>(destination->GetNativePtr());
	if (!deviceContext || !sourceTexture || !destinationTexture)
		return false;

	D3D11_TEXTURE2D_DESC sourceDesc = {};
	D3D11_TEXTURE2D_DESC destinationDesc = {};
	sourceTexture->GetDesc(&sourceDesc);
	destinationTexture->GetDesc(&destinationDesc);

	if (sourceDesc.Width != destinationDesc.Width ||
		sourceDesc.Height != destinationDesc.Height ||
		sourceDesc.Format != destinationDesc.Format ||
		sourceDesc.ArraySize != destinationDesc.ArraySize)
		return false;

	if (sourceDesc.SampleDesc.Count == destinationDesc.SampleDesc.Count && sourceDesc.SampleDesc.Quality == destinationDesc.SampleDesc.Quality)
	{
		if (sourceDesc.MipLevels == destinationDesc.MipLevels)
		{
			deviceContext->CopyResource(destinationTexture, sourceTexture);
		}
		else
		{
			const UINT subresourceCount = (std::min)(sourceDesc.ArraySize, destinationDesc.ArraySize);
			for (UINT arraySlice = 0; arraySlice < subresourceCount; ++arraySlice)
			{
				const UINT sourceSubresource = D3D11CalcSubresource(0, arraySlice, sourceDesc.MipLevels);
				const UINT destinationSubresource = D3D11CalcSubresource(0, arraySlice, destinationDesc.MipLevels);
				deviceContext->CopySubresourceRegion(destinationTexture, destinationSubresource, 0, 0, 0, sourceTexture, sourceSubresource, nullptr);
			}
		}
		return true;
	}

	if (sourceDesc.SampleDesc.Count > 1 && destinationDesc.SampleDesc.Count == 1)
	{
		deviceContext->ResolveSubresource(destinationTexture, 0, sourceTexture, 0, sourceDesc.Format);
		return true;
	}

	LOG_WARN("OIDN: unsupported texture copy path between sample counts %u and %u", sourceDesc.SampleDesc.Count, destinationDesc.SampleDesc.Count);
	return false;
}

void OIDN::BuildFrameData(HexEngine::DenoiserFrameData& fd, HexEngine::ITexture2D* diffuseSignalInput, HexEngine::ITexture2D* diffuseHitDistance, HexEngine::ITexture2D* specularSignalInput, HexEngine::ITexture2D* specularHitDistance, HexEngine::ITexture2D* normalAndDepth, HexEngine::ITexture2D* material, HexEngine::ITexture2D* motionVectors)
{
	fd.diffuseSignalInput = diffuseSignalInput;
	fd.diffuseHitDistance = diffuseHitDistance;
	fd.specularSignalInput = specularSignalInput;
	fd.specularHitDistance = specularHitDistance;
	fd.normalAndDepth = normalAndDepth;
	fd.material = material;
	fd.motionVectors = motionVectors;
}

void OIDN::FilterFrame(const HexEngine::DenoiserFrameData& fd, HexEngine::ITexture2D* output)
{
	if (!fd.specularSignalInput || !fd.normalAndDepth || !output)
		return;

	if ((!_created && !Create()) ||
		!RecreateResources(
			fd.specularSignalInput->GetWidth(),
			fd.specularSignalInput->GetHeight(),
			fd.specularSignalInput,
			output))
	{
		fd.specularSignalInput->CopyTo(output);
		return;
	}

	if (!CopyTexture(fd.specularSignalInput, _sharedColour))
	{
		fd.specularSignalInput->CopyTo(output);
		return;
	}

	_filter.execute();
	LogLastError("execute");

	if (!CopyTexture(_sharedOutput, output))
	{
		fd.specularSignalInput->CopyTo(output);
		return;
	}

	HexEngine::g_pEnv->GetGraphicsDevice().ResetState();
}

void OIDN::LogLastError(const char* operation)
{
	if (!_device)
		return;

	const char* errorMessage = nullptr;
	if (_device.getError(errorMessage) != oidn::Error::None)
		LOG_CRIT("OIDN %s failed: %s", operation, errorMessage ? errorMessage : "unknown error");
}
