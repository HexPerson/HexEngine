
#pragma once

#include "ITexture2D.hpp"
#include "IShader.hpp"
#include "BlurEffect.hpp"

namespace HexEngine
{
	class Camera;

	class Bloom
	{
	public:
		~Bloom();

		void Create(int32_t width, int32_t height);
		void Destroy();
		void Render(Camera* camera, ITexture2D* bloomInput, ITexture2D* bloomOutput);
		void Blit(ID3D11RenderTargetView* backBufferRTV);

	private:
		ITexture2D* _renderTarget = nullptr;
		BlurEffect* _blur = nullptr;
		std::shared_ptr<IShader> _renderShader;
		//IShader* _gaussianBlurShader = nullptr;
		//IShader* _gaussianBlurShaderVert = nullptr;
		D3D11_VIEWPORT _viewport;
	};
}
