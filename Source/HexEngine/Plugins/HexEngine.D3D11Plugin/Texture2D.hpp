

#pragma once

#include <HexEngine.Core/Graphics/ITexture2D.hpp>

namespace HexEngine
{
	class Texture2D : public ITexture2D
	{
		friend class GraphicsDeviceD3D11;
		friend class TextureLoader;

	public:
		virtual ~Texture2D();

		virtual int32_t GetWidth() override { return _width; }

		virtual int32_t GetHeight() override { return _height; }

		virtual void Destroy() override;

		virtual void* GetNativePtr() override;

		virtual void SetPixels(uint8_t* data, uint32_t size) override;

		virtual void SaveToFile(const fs::path& path) override;

		virtual uint32_t GetFormat() override;

		virtual void ClearDepth(uint32_t flags) override;

		virtual void CopyTo(ITexture2D* other) override;

		virtual void CopyTo(ITexture2D* other, const RECT& srcRect, const RECT& dstRect) override;

		virtual void BlendTo_Additive(ITexture2D* other, IShader* optionalShader) override;

		virtual void BlendTo_Additive_Double(ITexture2D* other, IShader* optionalShader) override;

		virtual void BlendTo_Alpha(ITexture2D* other, IShader* optionalShader) override;

		virtual void BlendTo_NonPremultiplied(ITexture2D* other, IShader* optionalShader) override;

		virtual void ClearRenderTargetView(const math::Color& colour) override;

		virtual void GetPixels(std::vector<uint8_t>& buffer) override;

		virtual void GetPixels(std::vector<float>& buffer) override;

		virtual void* GetSharedHandle() override;

	public:
		ID3D11Texture2D* _texture = nullptr;
		DXGI_FORMAT _format = DXGI_FORMAT::DXGI_FORMAT_UNKNOWN;
		int32_t _width = 0;
		int32_t _height = 0;
		ID3D11RenderTargetView* _renderTargetView = nullptr;
		ID3D11ShaderResourceView* _shaderResourceView = nullptr;
		ID3D11DepthStencilView* _depthStencilView = nullptr;

		D3D11_RTV_DIMENSION _rtvDimension;
		D3D11_SRV_DIMENSION _srvDimension;
		D3D11_DSV_DIMENSION _dsvDimension;
	};
}
