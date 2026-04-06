
#pragma once

#include "Dialog.hpp"
#include "TextureSearch.hpp"
#include "ComponentWidget.hpp"
#include "DragFloat.hpp"
#include "AssetSearch.hpp"

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
		void ApplyTextureFromSearch(MaterialTexture type, const AssetSearchResult& result);

	private:
		ScrollView* _rootView = nullptr;
		ComponentWidget* _layout = nullptr;
		AssetSearch* _textures[MaterialTexture::Count] = { nullptr };
		DragFloat* _smoothness = nullptr;
		DragFloat* _specularProbability = nullptr;
		std::shared_ptr<Material> _material;
	};
}
