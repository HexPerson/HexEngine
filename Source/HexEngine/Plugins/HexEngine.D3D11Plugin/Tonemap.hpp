

#pragma once

#include "Texture2D.hpp"
#include <HexEngine.Core/Graphics/IShader.hpp>

namespace HexEngine
{
	class Tonemap
	{
	public:
		void Create(int32_t width, int32_t height);
		void Destroy();
		void Render(ID3D11ShaderResourceView* bbSRV);
		void Blit(ID3D11RenderTargetView* backBufferRTV);

	private:
		Texture2D* _renderTarget = nullptr;
		std::shared_ptr<IShader> _renderShader;
	};
}
