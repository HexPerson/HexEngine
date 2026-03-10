
#pragma once

#include <HexEngine.Core\HexEngine.hpp>
#include <GFSDK_SSAO.h>

class HBAOPlus : public HexEngine::ISSAOProvider
{
public:
	virtual bool Create() override;

	virtual void Destroy() override;

	virtual void ApplyAmbientOcclusion(HexEngine::Camera* camera, HexEngine::ITexture2D* depthBuffer, HexEngine::ITexture2D* normals, HexEngine::ITexture2D* target) override;

private:
	GFSDK_SSAO_Context_D3D11* _aoContext = nullptr;
};
