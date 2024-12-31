

#include "NRDInterface.hpp"

void* Allocate(void* userArg, size_t size, size_t alignment)
{
	return malloc(size);
}

void* Reallocate(void* userArg, void* memory, size_t size, size_t alignment)
{
	return realloc(memory, size);
}

void Free(void* userArg, void* memory)
{
	free(memory);
}

bool NRDInterface::Create()
{
	return true;
}

void NRDInterface::Destroy()
{
	nrd::DestroyInstance(*_instance);
}

void NRDInterface::CreateBuffers(int32_t width, int32_t height, ITexture2D* beauty, ITexture2D* normals, ITexture2D* albedo)
{
	if (_created)
		return;

	const auto& desc = nrd::GetLibraryDesc();

	std::vector<nrd::DenoiserDesc> denoisers;
	for (uint32_t i = 0; i < desc.supportedDenoisersNum; ++i)
	{
		nrd::DenoiserDesc denoiser;

		denoiser.denoiser = desc.supportedDenoisers[i];
		denoiser.identifier = i;

		denoisers.push_back(denoiser);
	}

	nrd::InstanceCreationDesc createDesc;
	createDesc.denoisersNum = desc.supportedDenoisersNum;
	createDesc.denoisers = denoisers.data();
	createDesc.allocationCallbacks.Allocate = Allocate;
	createDesc.allocationCallbacks.Reallocate = Reallocate;
	createDesc.allocationCallbacks.Free = Free;

	auto res = nrd::CreateInstance(createDesc, _instance);

	if (res != nrd::Result::SUCCESS)
	{
		LOG_CRIT("Failed to start NRD");
		return;
	}

	_created = true;
}

void NRDInterface::BuildFrameData(DenoiserFrameData& fd, ITexture2D* beauty, ITexture2D* normals, ITexture2D* albedo)
{
	
}

void NRDInterface::FilterFrame(const DenoiserFrameData& fd, ITexture2D* beauty)
{
	nrd::CommonSettings cs;

	memcpy(cs.viewToClipMatrix, fd.camera->GetProjectionMatrix().m, sizeof(cs.viewToClipMatrix));
	memcpy(cs.viewToClipMatrixPrev, fd.camera->GetProjectionMatrixPrev().m, sizeof(cs.viewToClipMatrixPrev));

	memcpy(cs.worldToViewMatrix, fd.camera->GetViewMatrix().m, sizeof(cs.worldToViewMatrix));
	memcpy(cs.worldToViewMatrixPrev, fd.camera->GetViewMatrixPrev().m, sizeof(cs.worldToViewMatrixPrev));

	cs.cameraJitter[0] = (fd.jitter.x / 2.0f) * fd.camera->GetViewport().width;
	cs.cameraJitter[1] = (fd.jitter.y / 2.0f) * fd.camera->GetViewport().height;

	cs.frameIndex = (uint32_t)g_pEnv->_timeManager->_frameCount;

	cs.resourceSize[0] = beauty->GetWidth();
	cs.resourceSize[1] = beauty->GetHeight();

	cs.resourceSizePrev[0] = beauty->GetWidth();
	cs.resourceSizePrev[1] = beauty->GetHeight();

	cs.rectSize[0] = beauty->GetWidth();
	cs.rectSize[1] = beauty->GetHeight();

	cs.rectSizePrev[0] = beauty->GetWidth();
	cs.rectSizePrev[1] = beauty->GetHeight();
	
	nrd::SetCommonSettings(*_instance, cs);

	// denoiser settings

	//nrd::SetDenoiserSettings(*_instance, 0, 

	const nrd::DispatchDesc* dispatchDescs = nullptr;
	uint32_t dispatchDescsNum = 0;

	nrd::Identifier identifier = 0;
	nrd::GetComputeDispatches(
		*_instance,
		&identifier, 1,
		dispatchDescs, dispatchDescsNum);

	for (uint32_t i = 0; i < dispatchDescsNum; i++)
	{
		const nrd::DispatchDesc& dispatchDesc = dispatchDescs[i];
	}
}