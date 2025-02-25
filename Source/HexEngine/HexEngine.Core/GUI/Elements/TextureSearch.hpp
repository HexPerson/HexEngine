
#pragma once

#include "LineEdit.hpp"

namespace HexEngine
{
	class HEX_API TextureSearch : public Element
	{
	public:
		TextureSearch(
			Element* parent,
			const Point& position,
			const Point& size,
			const std::wstring& label,
			const std::shared_ptr<ITexture2D>& texture,
			std::shared_ptr<Material> material,
			MaterialTexture type);

		virtual ~TextureSearch();

		virtual void Render(GuiRenderer* renderer, uint32_t w, uint32_t h) override;

		virtual bool OnInputEvent(InputEvent event, InputData* data) override;

		virtual int32_t GetLabelWidth() const override;
		virtual void SetLabelMinSize(int32_t minSize) override;

	private:
		void DragAndDropTexture(const fs::path& path);
		void ConvertRMA();

	private:
		LineEdit* _edit = nullptr;
		std::shared_ptr<ITexture2D> _texture;
		std::shared_ptr<Material> _material = nullptr;
		MaterialTexture _type = MaterialTexture::Albedo;
	};
}
		
