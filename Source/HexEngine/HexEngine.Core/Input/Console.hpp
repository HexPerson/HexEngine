
#pragma once

#include "../Required.hpp"
#include "../GUI/GuiRenderer.hpp"
#include "../GUI/Elements/Canvas.hpp"

namespace HexEngine
{
	class Console
	{
	public:
		void Create();
		void Destroy();
		void Print(const char* text, ...);
		void Render(GuiRenderer* renderer);
		bool GetActive();
		void SetActive(bool active);
		void Toggle();

		void OnCharInput(wchar_t ch);
		void OnKeyInput(int32_t key, bool down);
		std::shared_ptr<IFontResource> GetFont() const;

	private:
		std::shared_ptr<IFontResource> _font;
		bool _active = false;
		std::string _input;
		std::list<std::string> _lines;
		std::vector<std::string> _history;
		int32_t _historyIdx = -1;
		std::mutex _lock;

		Canvas _canvas;
	};

#define CON_ECHO(text, ...) if(g_pEnv && g_pEnv->_commandManager &&  g_pEnv->_commandManager->GetConsole()) g_pEnv->_commandManager->GetConsole()->Print(text, __VA_ARGS__);
}
