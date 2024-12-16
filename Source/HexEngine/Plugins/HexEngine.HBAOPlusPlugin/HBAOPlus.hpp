
#pragma once

#include <HexEngine.Core\HexEngine.hpp>
#include <GFSDK_SSAO.h>

class HBAOPlus : public ISSAOProvider
{
public:
	virtual bool Create() override;

	virtual void Destroy() override;

	virtual void ApplyAmbientOcclusion(Camera* camera, ITexture2D* depthBuffer, ITexture2D* normals, ITexture2D* target) override;

private:
	GFSDK_SSAO_Context_D3D11* _aoContext = nullptr;
};
