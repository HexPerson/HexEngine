
#pragma once

#include "Dialog.hpp"
#include "TextureSearch.hpp"
#include "ComponentWidget.hpp"
#include "DragFloat.hpp"

namespace HexEngine
{
	class HEX_API MaterialDialog : public Dialog
	{
	public:
		MaterialDialog(Element* parent, const Point& position, const Point& size, const std::wstring& title, std::shared_ptr<Material> material);

		virtual ~MaterialDialog() {}

		bool Save();

		void AdditionalControls(Element* parent, MaterialTexture type, const Point& pos);

	private:
		ComponentWidget* _layout = nullptr;
		TextureSearch* _textures[MaterialTexture::Count] = { nullptr };
		TextureSearch* _normal = nullptr;
		DragFloat* _smoothness = nullptr;
		DragFloat* _specularProbability = nullptr;
		std::shared_ptr<Material> _material;
	};
}
