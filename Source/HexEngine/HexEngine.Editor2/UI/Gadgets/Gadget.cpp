
#include "Gadget.hpp"

namespace HexEditor
{
	Gadget::Gadget(const std::vector<char>& hotkeys, char confirmKey, char cancelKey)
	{
		for (auto& hk : hotkeys)
		{
			_hotkeys.push_back({ hk, false });
		}

		_confirmKey = confirmKey;
		_cancelKey = cancelKey;
	}

	bool Gadget::OnInputEvent(HexEngine::InputEvent event, HexEngine::InputData* data)
	{
		bool keyHandled = false;

		if(event == HexEngine::InputEvent::KeyDown)
		{
			keyHandled = ProcessKey(data->KeyDown.key);
		}
		else if (event == HexEngine::InputEvent::MouseDown)
		{
			// Mouse buttons should only confirm/cancel an already-running gadget.
			// Treating mouse-down as a hotkey source can retrigger gadgets from stale key state.
			if (_gadgetStarted)
			{
				keyHandled = ProcessKey(data->MouseDown.button);
			}
		}
		else if (event == HexEngine::InputEvent::KeyUp)
		{
			for (auto& hk : _hotkeys)
			{
				if (hk.first == data->KeyDown.key)
				{
					hk.second = false;
				}
			}
		}

		return _gadgetStarted || keyHandled;

	}

	bool Gadget::ProcessKey(char key)
	{
		if (_gadgetStarted)
		{
			if (key == _confirmKey)
			{
				_gadgetStarted = false;
				StopGadget(GadgetAction::Confirm);
				return true;
			}
			else if (key == _cancelKey)
			{
				_gadgetStarted = false;
				StopGadget(GadgetAction::Cancel);
				return true;
			}
		}

		bool allPressed = true;

		for (auto& hk : _hotkeys)
		{
			if (hk.first == key)
			{
				hk.second = true;
			}

			if (!hk.second)
				allPressed = false;
		}

		if (allPressed && _gadgetStarted == false)
		{
			_gadgetStarted = StartGadget();
		}

		return false;
	}
}
