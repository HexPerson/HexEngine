

#include "OIDN.hpp"

void ErrorFunc(void* userPtr, OIDNError code, const char* message)
{
	LOG_CRIT(message);
}
bool OIDN::Create()
{
	// Create an Open Image Denoise device
	

	for (int32_t i = 0; i < oidnGetNumPhysicalDevices(); ++i)
	{
		const char* deviceName = oidnGetPhysicalDeviceString(i, "name");
		auto deviceType = (OIDNDeviceType)oidnGetPhysicalDeviceInt(i, "type");

		size_t uuidSize = OIDN_UUID_SIZE;
		oidn::UUID uuid;
		memcpy(&uuid, oidnGetPhysicalDeviceData(i, "uuid", &uuidSize), uuidSize);
		//const auto uuid = (oidn::UUID);

		_device = oidn::newDevice(uuid); // CPU or GPU if available
		

		LOG_DEBUG("OIDN: %s is a %d", deviceName, deviceType);

		break;
	}
	bool systemMemorySupported = oidnGetDeviceBool(_device.getHandle(), "managedMemorySupported"); 
	OIDNExternalMemoryTypeFlag externalMemoryTypes = (OIDNExternalMemoryTypeFlag)oidnGetDeviceInt(_device.getHandle(), "externalMemoryTypes");
	oidnSetDeviceInt(_device.getHandle(), "verbos", 4);

	_device.commit();
	//auto deviceName = oidnGetPhysicalDeviceString(_device.g, "name");

	//LOG_DEBUG("OIDN created using device '%s'", deviceName.c_str());

	oidnSetDeviceErrorFunction(_device.getHandle(), ErrorFunc, 0);

	return true;
}

void OIDN::Destroy()
{
	if (_filter)
		_filter.release();

	if (_colourBuf)
		_colourBuf.release();

	if (_albedoBuf)
		_albedoBuf.release();

	if (_normalBuf)
		_normalBuf.release();
}

void OIDN::CreateBuffers(int32_t width, int32_t height, HexEngine::ITexture2D* diffuseSignalInput, HexEngine::ITexture2D* diffuseHitDistance, HexEngine::ITexture2D* specularSignalInput, HexEngine::ITexture2D* specularHitDistance, HexEngine::ITexture2D* normalAndDepth, HexEngine::ITexture2D* material, HexEngine::ITexture2D* motionVectors)
{
	if (_filter)
		_filter.release();

	if (_colourBuf)
		_colourBuf.release();

	if (_albedoBuf)
		_albedoBuf.release();

	if (_normalBuf)
		_normalBuf.release();

	if (_outputBuf)
		_outputBuf.release();

	// Create buffers for input/output images accessible by both host (CPU) and device (CPU/GPU)
	//_colourBuf = _device.newBuffer(width * height * 4 * sizeof(float), oidn::Storage::Managed);
	//_albedoBuf = _device.newBuffer(width * height * 4 * sizeof(float), oidn::Storage::Managed);
	//_normalBuf = _device.newBuffer(width * height * 4 * sizeof(float), oidn::Storage::Managed);
	//_outputBuf = _device.newBuffer(width * height * 4 * sizeof(float), oidn::Storage::Managed);

	/*_beautyStaging = HexEngine::g_pEnv->_graphicsDevice->CreateTexture2D(
		width, height,
		(DXGI_FORMAT)beauty->GetFormat(),
		1,
		0,
		0,
		1,
		0,
		D3D11_RTV_DIMENSION_UNKNOWN,
		D3D11_UAV_DIMENSION_UNKNOWN,
		D3D11_SRV_DIMENSION_UNKNOWN,
		D3D11_DSV_DIMENSION_UNKNOWN,
		D3D11_USAGE_DEFAULT,
		D3D11_RESOURCE_MISC_SHARED);

	_normalStaging = HexEngine::g_pEnv->_graphicsDevice->CreateTexture2D(
		width, height,
		(DXGI_FORMAT)beauty->GetFormat(),
		1,
		0,
		0,
		1,
		0,
		D3D11_RTV_DIMENSION_UNKNOWN,
		D3D11_UAV_DIMENSION_UNKNOWN,
		D3D11_SRV_DIMENSION_UNKNOWN,
		D3D11_DSV_DIMENSION_UNKNOWN,
		D3D11_USAGE_DEFAULT,
		D3D11_RESOURCE_MISC_SHARED);

	_colourStaging = HexEngine::g_pEnv->_graphicsDevice->CreateTexture2D(
		width, height,
		(DXGI_FORMAT)beauty->GetFormat(),
		1,
		0,
		0,
		1,
		0,
		D3D11_RTV_DIMENSION_UNKNOWN,
		D3D11_UAV_DIMENSION_UNKNOWN,
		D3D11_SRV_DIMENSION_UNKNOWN,
		D3D11_DSV_DIMENSION_UNKNOWN,
		D3D11_USAGE_DEFAULT,
		D3D11_RESOURCE_MISC_SHARED);

	_outputStaging = HexEngine::g_pEnv->_graphicsDevice->CreateTexture2D(
		width, height,
		(DXGI_FORMAT)beauty->GetFormat(),
		1,
		0,
		0,
		1,
		0,
		D3D11_RTV_DIMENSION_UNKNOWN,
		D3D11_UAV_DIMENSION_UNKNOWN,
		D3D11_SRV_DIMENSION_UNKNOWN,
		D3D11_DSV_DIMENSION_UNKNOWN,
		D3D11_USAGE_DEFAULT,
		D3D11_RESOURCE_MISC_SHARED);*/

	_outputStaging = HexEngine::g_pEnv->_graphicsDevice->CreateTexture(specularSignalInput);

	_colourBuf = _device.newBuffer(oidn::ExternalMemoryTypeFlag::D3D11ResourceKMT, specularSignalInput->GetSharedHandle(), nullptr, width * height * 4 * sizeof(float));
	_albedoBuf = _device.newBuffer(oidn::ExternalMemoryTypeFlag::D3D11ResourceKMT, normalAndDepth->GetSharedHandle(), nullptr, width * height * 4 * sizeof(float));
	_normalBuf = _device.newBuffer(oidn::ExternalMemoryTypeFlag::D3D11ResourceKMT, material->GetSharedHandle(), nullptr, width * height * 4 * sizeof(float));
	_outputBuf = _device.newBuffer(oidn::ExternalMemoryTypeFlag::D3D11ResourceKMT, _outputStaging->GetSharedHandle(), nullptr, width * height * 4 * sizeof(float));

	// Create a filter for denoising a beauty (color) image using optional auxiliary images too
	// This can be an expensive operation, so try no to create a new filter for every image!
	_filter = _device.newFilter("RT"); // generic ray tracing filter

	_filter.setImage("color",  _colourBuf, oidn::Format::Float3, width, height, 0, 16, width * 16); // beauty
	_filter.setImage("albedo", _albedoBuf, oidn::Format::Float3, width, height, 0, 16, width * 16); // auxiliary
	_filter.setImage("normal", _normalBuf, oidn::Format::Float3, width, height, 0, 16, width * 16); // auxiliary
	_filter.setImage("output", _colourBuf, oidn::Format::Float3, width, height, 0, 16, width * 16); // denoised beauty
	_filter.set("hdr", false); // beauty image is HDR
	_filter.set("quality", OIDNQuality::OIDN_QUALITY_FAST); // beauty image is HDR
	_filter.commit();

	
}

void OIDN::BuildFrameData(HexEngine::DenoiserFrameData& fd, HexEngine::ITexture2D* diffuseSignalInput, HexEngine::ITexture2D* diffuseHitDistance, HexEngine::ITexture2D* specularSignalInput, HexEngine::ITexture2D* specularHitDistance, HexEngine::ITexture2D* normalAndDepth, HexEngine::ITexture2D* material, HexEngine::ITexture2D* motionVectors)
{
	//return;
	//beauty->CopyTo(_beautyStaging);
	//_beautyStaging->GetPixels(fd.colour);

	//normals->CopyTo(_normalStaging);
	//_normalStaging->GetPixels(fd.normals);

	//albedo->CopyTo(_colourStaging);
	//_colourStaging->GetPixels(fd.albedo);
}

void OIDN::FilterFrame(const HexEngine::DenoiserFrameData& fd, HexEngine::ITexture2D* output)
{
	// Fill the input image buffers
	//float* colorPtr = (float*)_colourBuf.getData();
	//_colourBuf.write(0, fd.colour.size() /** sizeof(float)*/, fd.colour.data());

	//float* albedoPtr = (float*)_albedoBuf.getData();
	//_albedoBuf.write(0, fd.albedo.size() /** sizeof(float)*/, fd.albedo.data());

	//float* normalPtr = (float*)_normalBuf.getData();
	//_normalBuf.write(0, fd.normals.size() /** sizeof(float)*/, fd.normals.data());

	//_colourBuf.write(

	// Filter the beauty image
	_filter.execute();

	// Check for errors
	const char* errorMessage;
	if (_device.getError(errorMessage) != oidn::Error::None)
	{
		LOG_CRIT("Error: %s\n", errorMessage);
	}
	else
	{
		//_outputBuf.read(0, fd.colour.size(), (void*)fd.colour.data());

		//_beautyStaging->SetPixels((uint8_t*)_colourBuf.getData(), fd.colour.size());
		//_colourBuf->CopyTo(beauty);
	}
}


