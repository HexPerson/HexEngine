#pragma once

#include "Dialog.hpp"

namespace HexEngine
{
	class ParticleEffect;
	class ComponentWidget;
	class AssetSearch;
	class ScrollView;
	struct AssetSearchResult;
	class LineEdit;

	class HEX_API ParticleEffectDialog : public Dialog
	{
	public:
		ParticleEffectDialog(Element* parent, const Point& position, const Point& size, const std::wstring& title, const std::shared_ptr<ParticleEffect>& effect);
		virtual ~ParticleEffectDialog() override {}

	private:
		bool SaveAndClose();
		void OnPickMaterial(AssetSearch* search, const AssetSearchResult& result);
		void OnPickTexture(AssetSearch* search, const AssetSearchResult& result);
		void OnSetName(LineEdit* edit, const std::wstring& value);

	private:
		std::shared_ptr<ParticleEffect> _effect;
		ScrollView* _rootView = nullptr;
		ComponentWidget* _layout = nullptr;
		AssetSearch* _materialSearch = nullptr;
		AssetSearch* _textureSearch = nullptr;
	};
}
