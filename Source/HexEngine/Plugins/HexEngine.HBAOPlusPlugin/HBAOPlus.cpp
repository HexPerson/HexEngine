
#include "HBAOPlus.hpp"
#include "../HexEngine.D3D11Plugin/Texture2D.hpp"

HexEngine::HVar hbao_unitscale("hbao_unitscale", "Metres to view-space units", 1.0f, 0.1f, 100.0f);

bool HBAOPlus::Create()
{
	GFSDK_SSAO_Status status;	

	status = GFSDK_SSAO_CreateContext_D3D11(
		(ID3D11Device*)HexEngine::g_pEnv->_graphicsDevice->GetNativeDevice(),
		&_aoContext, nullptr);

	if (status != GFSDK_SSAO_OK)
	{
		LOG_CRIT("HBAOPlus failed to initialize: %d", status);
		return false;
	}

	//g_pEnv->_commandManager->RegisterVar(&hbao_unitscale);

	return true;
}

void HBAOPlus::Destroy()
{
	SAFE_RELEASE(_aoContext);
}

void HBAOPlus::ApplyAmbientOcclusion(HexEngine::Camera* camera, HexEngine::ITexture2D* depthBuffer, HexEngine::ITexture2D* normals, HexEngine::ITexture2D* target)
{
	const math::Matrix& pm = camera->GetProjectionMatrix();

	Texture2D* tex11Depth = dynamic_cast<Texture2D*>(depthBuffer);

	GFSDK_SSAO_InputData_D3D11 Input;
	Input.DepthData.DepthTextureType = GFSDK_SSAO_HARDWARE_DEPTHS;
	Input.DepthData.pFullResDepthTextureSRV = tex11Depth->_shaderResourceView;
	Input.DepthData.ProjectionMatrix.Data = GFSDK_SSAO_Float4x4(&pm.m[0][0]);
	Input.DepthData.ProjectionMatrix.Layout = GFSDK_SSAO_ROW_MAJOR_ORDER;
	Input.DepthData.MetersToViewSpaceUnits = hbao_unitscale._val.f32;

	math::Matrix vp = camera->GetViewMatrix();// *camera->GetProjectionMatrix();

	//vp = vp.Invert();
	//vp = vp.Transpose();

	if (normals)
	{
		Input.NormalData.Enable = true;
		Input.NormalData.pFullResNormalTextureSRV = ((Texture2D*)normals)->_shaderResourceView;
		Input.NormalData.WorldToViewMatrix.Data = GFSDK_SSAO_Float4x4(&vp.m[0][0]);
		Input.NormalData.WorldToViewMatrix.Layout = GFSDK_SSAO_ROW_MAJOR_ORDER;
	}
	else
	{
		Input.NormalData.Enable = false;
	}

	GFSDK_SSAO_Parameters Params;
	Params.Radius = 1.0f;
	Params.Bias = 0.1f;
	Params.PowerExponent = 1.0f;
	Params.Blur.Enable = true;
	Params.Blur.Radius = GFSDK_SSAO_BLUR_RADIUS_4;
	Params.Blur.Sharpness = 16.f;
	Params.EnableDualLayerAO = false;

	Params.DepthThreshold.Enable = true;
	Params.DepthThreshold.MaxViewDepth = camera->GetFarZ() * 0.7f;

	/*Params.ForegroundAO.Enable = true;
	Params.ForegroundAO.ForegroundViewDepth = 10.0f;

	Params.BackgroundAO.Enable = true;
	Params.BackgroundAO.BackgroundViewDepth = 600.0f;*/

	GFSDK_SSAO_Output_D3D11 Output;
	Output.pRenderTargetView = ((Texture2D*)target)->_renderTargetView;
	Output.Blend.Mode = GFSDK_SSAO_MULTIPLY_RGB;

	_aoContext->RenderAO((ID3D11DeviceContext*)HexEngine::g_pEnv->_graphicsDevice->GetNativeDeviceContext(), Input, Params, Output);
}