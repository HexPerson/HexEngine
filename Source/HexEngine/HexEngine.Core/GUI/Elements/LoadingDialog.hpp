
#pragma once

#include "Dialog.hpp"

namespace HexEngine
{
	class HEX_API LoadingDialog : public Dialog
	{
	public:
		LoadingDialog(Element* parent, const Point& position, const Point& size, const std::wstring& title);

		void SetPercentage(float percentage);
		void SetText(const std::wstring& text);

		virtual void Render(GuiRenderer* renderer, uint32_t w, uint32_t h) override;

	private:
		float _percentage = 0.0f;
		std::wstring _text;
		std::recursive_mutex _lock;
	};
}
