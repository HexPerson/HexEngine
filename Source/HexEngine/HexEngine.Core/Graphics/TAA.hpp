
#pragma once

#include "../Required.hpp"
#include "../Environment/IEnvironment.hpp"
#include "../Graphics/ITexture2D.hpp"
#include "../GUI/GuiRenderer.hpp"

namespace HexEngine
{
	class TAA
	{
	public:
		bool Create(ITexture2D* buffer);

		void Destroy();

		~TAA();

		 math::Vector2 GetJitterOffset(float screenWidth, float screenHeight) const;

		 void Resolve(ITexture2D* output, ITexture2D* buffer, ITexture2D* velocity, ITexture2D* normalAndDepth, GuiRenderer* renderer);

	private:
		ITexture2D* _history = nullptr;
		ITexture2D* _renderTarget = nullptr;
		//ITexture2D* _velocity = nullptr;
		IShader* _resolveShader = nullptr;
	};
}
