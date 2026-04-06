
#pragma once

#include "../Required.hpp"
#include "../GUI/GuiRenderer.hpp"
#include "../GUI/Elements/Canvas.hpp"

namespace HexEngine
{
	class HEX_API Console
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

	private:
		bool _active = false;
		std::string _input;
		std::list<std::string> _lines;
		std::vector<std::string> _history;
		int32_t _historyIdx = -1;
		std::mutex _lock;

		Canvas _canvas;
	};

#define CON_ECHO(text, ...) if(HexEngine::g_pEnv && HexEngine::g_pEnv->_commandManager &&  HexEngine::g_pEnv->_commandManager->GetConsole()) HexEngine::g_pEnv->_commandManager->GetConsole()->Print(text, __VA_ARGS__);
}
