

#pragma once

#include "Element.hpp"

namespace HexEngine
{
	class HEX_API LineEdit : public Element
	{
	public:
		using OnSetInputFn = std::function<void(LineEdit*, const std::wstring&)>;
		using OnDragAndDropFn = std::function<void(LineEdit*, const fs::path&)>;
		using OnDoubleClickFn = std::function<void(LineEdit*, const std::wstring&)>;

		LineEdit(Element* parent, const Point& position, const Point& size, const std::wstring& label);

		virtual void Render(GuiRenderer* renderer, uint32_t w, uint32_t h) override;

		void SetValue(const std::wstring& value);
		const std::wstring& GetValue() const;

		//int32_t GetLabelWidth() const;

		virtual bool OnInputEvent(InputEvent event, InputData* data) override;

		void SetOnInputFn(OnSetInputFn fn);
		void SetOnDragAndDropFn(OnDragAndDropFn fn);
		void SetOnDoubleClickFn(OnDoubleClickFn fn);

		virtual int32_t GetLabelWidth() const override;

		void SetIcon(const std::shared_ptr<ITexture2D>& icon, const math::Color& colour);
		void SetDoesCallbackWaitForReturn(bool doesWait);
		void SetUneditableText(const std::wstring& text);

	private:
		std::wstring _label;
		std::wstring _value;		
		OnSetInputFn _onInputFn;
		OnDragAndDropFn _onDragAndDropFn;
		OnDoubleClickFn _onDoubleClickFn;
		std::shared_ptr<ITexture2D> _icon;
		math::Color _iconColour;
		bool _hasLabelChanged = false;
		int32_t _cachedLabelWidth = 0;
		bool _doesCallbackWaitForReturn = true;
		std::wstring _uneditableText;

	protected:
		bool _hovering = false;
	};
}
