
#pragma once

#include <HexEngine.Core/HexEngine.hpp>
#include "NRD.h"

class NRDInterface : public HexEngine::IDenoiserProvider
{
public:
	virtual bool Create() override;

	virtual void Destroy() override;

	virtual void CreateBuffers(int32_t width, int32_t height, HexEngine::ITexture2D* beauty, HexEngine::ITexture2D* normals, HexEngine::ITexture2D* albedo) override;

	virtual void BuildFrameData(HexEngine::DenoiserFrameData& fd, HexEngine::ITexture2D* beauty, HexEngine::ITexture2D* normals, HexEngine::ITexture2D* albedo) override;

	virtual void FilterFrame(const HexEngine::DenoiserFrameData& fd, HexEngine::ITexture2D* beauty) override;

private:
	nrd::Instance* _instance;
	bool _created = false;
};
