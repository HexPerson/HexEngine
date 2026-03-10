

#pragma once

#include <HexEngine.Core/Graphics/ITexture3D.hpp>

class Texture3D : public HexEngine::ITexture3D
{
	friend class GraphicsDeviceD3D11;
	friend class TextureLoader;

public:
	virtual ~Texture3D();

	virtual int32_t GetWidth() override { return _width; }

	virtual int32_t GetHeight() override { return _height; }

	virtual int32_t GetDepth() override { return _depth; }

	virtual void Destroy() override;

	virtual void* GetNativePtr() override;

	virtual void SetPixels(uint8_t* data, uint32_t size, int32_t slice = 0) override;

	virtual void SaveToFile(const fs::path& path) override;

	virtual uint32_t GetFormat() override;

	virtual void ClearDepth(uint32_t flags) override;

	virtual void CopyTo(ITexture3D* other) override;

	virtual void ClearRenderTargetView(const math::Color& colour) override;

	virtual void GetPixels(std::vector<uint8_t>& buffer) override;

	virtual void GetPixels(std::vector<float>& buffer) override {}

	virtual void* GetSharedHandle() override;

	virtual void* LockPixels(int32_t* rowPitch = nullptr) override;
	virtual void UnlockPixels() override;

private:
	ID3D11Texture3D* _texture = nullptr;
	DXGI_FORMAT _format = DXGI_FORMAT::DXGI_FORMAT_UNKNOWN;
	int32_t _width = 0;
	int32_t _height = 0;
	int32_t _depth = 0;
	ID3D11RenderTargetView* _renderTargetView = nullptr;
	ID3D11ShaderResourceView* _shaderResourceView = nullptr;
	ID3D11DepthStencilView* _depthStencilView = nullptr;

	D3D11_RTV_DIMENSION _rtvDimension;
	D3D11_SRV_DIMENSION _srvDimension;
	D3D11_DSV_DIMENSION _dsvDimension;
};