
#pragma once

#include <HexEngine.Core/HexEngine.hpp>
#include "NRD.h"

class NRDInterface : public IDenoiserProvider
{
public:
	virtual bool Create() override;

	virtual void Destroy() override;

	virtual void CreateBuffers(int32_t width, int32_t height, ITexture2D* beauty, ITexture2D* normals, ITexture2D* albedo) override;

	virtual void BuildFrameData(DenoiserFrameData& fd, ITexture2D* beauty, ITexture2D* normals, ITexture2D* albedo) override;

	virtual void FilterFrame(const DenoiserFrameData& fd, ITexture2D* beauty) override;

private:
	nrd::Instance* _instance;
	bool _created = false;
};
