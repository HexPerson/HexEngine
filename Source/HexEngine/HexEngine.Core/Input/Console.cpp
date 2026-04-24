
#include "Console.hpp"
#include "HCommand.hpp"
#include "CommandManager.hpp"
#include "../GUI/Elements/Element.hpp"

namespace HexEngine
{
	const int32_t ConsoleFontSize = (int32_t)Style::FontSize::Tiny;
	const int32_t kConsoleMaxLines = 1024;

	HEX_COMMAND(ConsoleToggle)
	{
		if(pressed)
			g_pEnv->_commandManager->GetConsole()->Toggle();
	}

	void Console::Create()
	{
		// Bind the console to the default key
		//g_pEnv->_commandManager->RegisterCommand(&cmd_ConsoleToggle);
		g_pEnv->_commandManager->CreateBind(VK_OEM_8, "ConsoleToggle");

		uint32_t width, height;
		g_pEnv->_graphicsDevice->GetBackBufferDimensions(width, height);

		height >>= 1;

		_canvas.Create(width, height);
		
	}

	void Console::Destroy()
	{
	}

	void Console::Render(GuiRenderer* renderer)
	{
		uint32_t width, height;
		g_pEnv->_graphicsDevice->GetBackBufferDimensions(width, height);

		height >>= 1;

		if (_canvas.BeginDraw(renderer, width, height /*<< 1*/))
		{
			renderer->FillQuad(0, 0, width, height, math::Color(HEX_RGBA_TO_FLOAT4(40, 40, 40, 180)));
			renderer->Line(0, height, width, height, math::Color(HEX_RGBA_TO_FLOAT4(5, 5, 5, 255)));

			std::wstring input = L"] ";

			if (_input.length() > 0)
			{
				input.insert(input.end(), _input.begin(), _input.end());

				auto vars = g_pEnv->_commandManager->FindHVars(_input);

				int32_t promptY = height;

				for (auto var : vars)
				{
					auto txt = std::wstring(var->_name.begin(), var->_name.end());
					renderer->PrintText(renderer->_style.font.get(), ConsoleFontSize, 15, promptY, math::Color(1, 1, 1, 1), 0, txt);
					promptY += ConsoleFontSize;
				}
			}

			renderer->PrintText(renderer->_style.font.get(), ConsoleFontSize, 5, height - ConsoleFontSize, math::Color(1, 1, 1, 1), 0, input);

			static const math::Color colourTable[] = {
				math::Color(HEX_RGBA_TO_FLOAT4(237, 28, 36, 255)),		// r
				math::Color(HEX_RGBA_TO_FLOAT4(10, 255, 64, 255)),		// g
				math::Color(HEX_RGBA_TO_FLOAT4(2, 109, 207, 255)),		// b
				math::Color(HEX_RGBA_TO_FLOAT4(230, 218, 4, 255)),		// y
				math::Color(HEX_RGBA_TO_FLOAT4(245, 245, 245, 255)),	// w
			};

			std::unique_lock lock(_lock);

			int y = height - ConsoleFontSize * 2;
			int x = 5;
			for (auto& line : _lines)
			{
				std::wstring wline(line.begin(), line.end());
				math::Color col(1, 1, 1, 1);

				while (true)
				{
					if (auto p = wline.find_first_of('^'); p != wline.npos)
					{
						// render the un-formatted part first
						if (p > 0)
						{
							renderer->PrintText(renderer->_style.font.get(), ConsoleFontSize, 5, y, col, 0, wline.substr(0, p));
							wline.erase(p);
							p = 0;

							if (wline.length() == 0)
								break;
						}
						if (wline[p + 1] == 'r') col = colourTable[0];
						if (wline[p + 1] == 'g') col = colourTable[1];
						if (wline[p + 1] == 'b') col = colourTable[2];
						if (wline[p + 1] == 'y') col = colourTable[3];
						if (wline[p + 1] == 'w') col = colourTable[4];

						wline.erase(0, 2);

						int32_t tw, th;

						if (auto p2 = wline.find_first_of('^'); p2 != wline.npos)
						{
							auto sub = wline.substr(0, p2);
							renderer->PrintText(renderer->_style.font.get(), ConsoleFontSize, x, y, col, 0, sub);
							renderer->_style.font->MeasureText(ConsoleFontSize, sub, tw, th);

							wline.erase(0, p2);

							x += tw;
						}
						else
						{
							renderer->PrintText(renderer->_style.font.get(), ConsoleFontSize, x, y, col, 0, wline);
							x = 5;
							break;
						}
					}
					else
					{
						renderer->PrintText(renderer->_style.font.get(), ConsoleFontSize, 5, y, col, 0, wline);
						x = 5;
						break;
					}
				}

				y -= ConsoleFontSize + 2;
			}

			_canvas.EndDraw(renderer);
		}

		_canvas.Present(renderer, 0, 0, width, height /*<< 1*/);
	}

	void Console::Print(const char* text, ...)
	{
		va_list	va_alist;
		va_start(va_alist, text);
		char buf[1024];
		_vsnprintf_s(buf, _TRUNCATE, text, va_alist);
		va_end(va_alist);

		std::unique_lock lock(_lock);

		_lines.insert(_lines.begin(), buf);

		_canvas.Redraw();

		if (_lines.size() > kConsoleMaxLines)
		{
			_lines.pop_back();
		}

	}

	bool Console::GetActive()
	{
		return _active;
	}

	void Console::SetActive(bool active)
	{
		_active = active;
	}

	void Console::Toggle()
	{
		_active = !_active;
	}

	void Console::OnCharInput(wchar_t ch)
	{
		if (ch == VK_BACK)
		{
			if (_input.length() > 0)
			{
				_input.pop_back();

				_canvas.Redraw();
			}
			return;
		}
		else if (ch == VK_RETURN)
		{
			_history.insert(_history.begin(), _input);

			g_pEnv->_commandManager->ProcessCommandInput(_input);

			_input.clear();

			_canvas.Redraw();

			_historyIdx = 0;

			return;
		}

		if (std::isalnum(ch) || ch == ' ' || ch == '^' || ch == '_' || ch == '.' || ch == '-' || ch == '+')
		{
			_input.push_back(ch);

			_canvas.Redraw();
		}		
	}

	void Console::OnKeyInput(int32_t key, bool down)
	{
		if (key == VK_UP && down)
		{
			if (_history.size() > 0)
			{
				_historyIdx++;

				_historyIdx = std::clamp(_historyIdx, 0, (int32_t)(_history.size() - 1));

				_input = _history[_historyIdx];

				_canvas.Redraw();
			}
		}
		else if(key == VK_DOWN && down)
		{
			if (_history.size() > 0)
			{
				_historyIdx--;

				_historyIdx = std::clamp(_historyIdx, 0, (int32_t)(_history.size() - 1));

				_input = _history[_historyIdx];

				_canvas.Redraw();
			}
		}
	}
}