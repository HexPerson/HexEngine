
#pragma once

#include "ITexture2D.hpp"
#include "IShader.hpp"

namespace HexEngine
{
	enum class BlurType
	{
		Gaussian,
		Radial
	};

	class GuiRenderer;

	class BlurEffect
	{
	public:
		BlurEffect(ITexture2D* textureToBlur, BlurType type, int32_t blurSize);
		~BlurEffect();

		void Render(GuiRenderer* renderer, bool alpha = false);

	private:
		BlurType _type;
		int32_t _blurSize;
		ITexture2D* _blurTarget;
		ITexture2D* _blurCompositionRT;
		std::shared_ptr<IShader> _shaders[2];
	};
}
