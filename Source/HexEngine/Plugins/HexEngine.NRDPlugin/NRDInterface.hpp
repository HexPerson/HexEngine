#pragma once

#include <HexEngine.Core/HexEngine.hpp>
#include "NRD.h"

#include <d3d11.h>
#include <vector>

class NRDInterface : public HexEngine::IDenoiserProvider
{
public:
	virtual bool Create() override;
	virtual void Destroy() override;
	virtual void CreateBuffers(int32_t width, int32_t height, HexEngine::ITexture2D* signalInput, HexEngine::ITexture2D* hitDistance, HexEngine::ITexture2D* normalAndDepth, HexEngine::ITexture2D* material, HexEngine::ITexture2D* motionVectors) override;
	virtual void BuildFrameData(HexEngine::DenoiserFrameData& fd, HexEngine::ITexture2D* signalInput, HexEngine::ITexture2D* hitDistance, HexEngine::ITexture2D* normalAndDepth, HexEngine::ITexture2D* material, HexEngine::ITexture2D* motionVectors) override;
	virtual void FilterFrame(const HexEngine::DenoiserFrameData& fd, HexEngine::ITexture2D* output) override;

private:
	struct TextureBinding
	{
		ID3D11Texture2D* texture = nullptr;
		ID3D11ShaderResourceView* srv = nullptr;
		ID3D11UnorderedAccessView* uav = nullptr;
		ID3D11RenderTargetView* rtv = nullptr;
		DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
		uint32_t width = 0;
		uint32_t height = 0;
		bool ownsTexture = false;

		void Reset();
	};

	bool EnsureDevice();
	bool RecreateResources(int32_t width, int32_t height, HexEngine::ITexture2D* signalInput, HexEngine::ITexture2D* hitDistance, HexEngine::ITexture2D* normalAndDepth, HexEngine::ITexture2D* material, HexEngine::ITexture2D* motionVectors);
	void DestroyNrdResources();
	bool CreateInstance();
	bool CreatePipelines();
	bool CreateSamplers();
	bool CreateConstantBuffer();
	bool CompileFullscreenShaders();
	bool CreatePoolTextures();
	bool CreateAuxiliaryTextures(HexEngine::ITexture2D* signalInput);
	bool CreateTexture(TextureBinding& binding, uint32_t width, uint32_t height, DXGI_FORMAT format, UINT bindFlags);
	bool CreateExternalBinding(TextureBinding& binding, HexEngine::ITexture2D* texture);
	TextureBinding* ResolveResource(const nrd::ResourceDesc& resource);
	bool RunPreprocess(const HexEngine::DenoiserFrameData& fd);
	bool RunDenoiser(const HexEngine::DenoiserFrameData& fd);
	bool RunResolve(const HexEngine::DenoiserFrameData& fd, HexEngine::ITexture2D* output);
	void UnbindComputeResources(const nrd::DispatchDesc& dispatchDesc, const nrd::PipelineDesc& pipelineDesc);
	static DXGI_FORMAT GetDxgiFormat(nrd::Format format);
	static nrd::Denoiser GetSelectedDenoiser();
	bool IsUsingReblur() const;

private:
	ID3D11Device* _device = nullptr;
	ID3D11DeviceContext* _context = nullptr;
	nrd::Instance* _instance = nullptr;
	const nrd::InstanceDesc* _instanceDesc = nullptr;
	std::vector<ID3D11ComputeShader*> _pipelines;
	std::vector<TextureBinding> _permanentPool;
	std::vector<TextureBinding> _transientPool;
	TextureBinding _signalInput;
	TextureBinding _hitDistanceInput;
	TextureBinding _normalAndDepthInput;
	TextureBinding _materialInput;
	TextureBinding _motionVectorsInput;
	TextureBinding _normalRoughness;
	TextureBinding _viewZ;
	TextureBinding _specularRadianceHitDistance;
	TextureBinding _denoisedSpecularRadianceHitDistance;
	TextureBinding _resolvedSignal;
	ID3D11Buffer* _constantBuffer = nullptr;
	ID3D11SamplerState* _pointClampSampler = nullptr;
	ID3D11SamplerState* _linearClampSampler = nullptr;
	ID3D11VertexShader* _fullscreenVS = nullptr;
	ID3D11PixelShader* _preprocessPS = nullptr;
	ID3D11PixelShader* _resolvePS = nullptr;
	bool _created = false;
	bool _loggedCreateBuffers = false;
	bool _resetHistory = true;
	uint32_t _width = 0;
	uint32_t _height = 0;
	math::Vector2 _previousJitter;
	nrd::Denoiser _activeDenoiser = nrd::Denoiser::RELAX_SPECULAR;
};
