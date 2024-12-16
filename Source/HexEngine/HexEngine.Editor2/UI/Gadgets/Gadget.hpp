
#pragma once

#include <HexEngine.Core\HexEngine.hpp>

namespace HexEditor
{
	enum class GadgetAction
	{
		Confirm,
		Cancel
	};

	class Gadget
	{
	public:
		
		Gadget(const std::vector<char>& hotkeys, char confirmKey, char cancelKey);

		virtual bool OnInputEvent(InputEvent event, InputData* data);

		virtual void Update() {}

		virtual bool StartGadget() { return false; };

		virtual void StopGadget(GadgetAction action) {};

		bool IsStarted() const { return _gadgetStarted; }

	private:
		bool ProcessKey(char key);

	private:
		std::vector<std::pair<char, bool>> _hotkeys;
		char _confirmKey;
		char _cancelKey;

	protected:
		bool _gadgetStarted = false;
	};
}
