
#pragma once

#include "Element.hpp"

namespace HexEngine
{
	class HEX_API Checkbox : public Element
	{
	public:
		enum class CheckType
		{
			None,
			Bool,
			Callback
		};

		struct CheckData
		{
			~CheckData() {}

			bool Get()
			{
				switch (type)
				{
				case CheckType::Bool:
					return *b;

				case CheckType::Callback:
					return c();
				}
				return false;
			}

			void Toggle()
			{
				switch (type)
				{
				case CheckType::Bool:
					*b = !*b;
				}
			}

			union
			{
				bool* b = nullptr;
				std::function<bool()> c;
			};

			CheckType type = CheckType::None;
		};

		using OnCheckFn = std::function<void(Checkbox*, bool)>;

		Checkbox(Element* parent, const Point& position, const Point& size, const std::wstring& label, bool* value);
		Checkbox(Element* parent, const Point& position, const Point& size, const std::wstring& label, std::function<bool()> callback);

		~Checkbox();

		virtual void Render(GuiRenderer* renderer, uint32_t w, uint32_t h) override;
		virtual bool OnInputEvent(InputEvent event, InputData* data) override;

		void SetOnCheckFn(OnCheckFn fn);

		virtual int32_t GetLabelWidth() const;
		virtual std::wstring GetLabelText() const override;

	private:
		CheckData _value;
		std::wstring _label;
		std::shared_ptr<ITexture2D> _tickImg;
		bool _hovering = false;
		OnCheckFn _onCheckFn;
	};
}
