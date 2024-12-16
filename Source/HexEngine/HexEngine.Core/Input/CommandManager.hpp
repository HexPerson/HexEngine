
#pragma once

#include "../Required.hpp"
#include "Hvar.hpp"
//#include "HCommand.hpp"
#include "Console.hpp"
#include "InputSystem.hpp"
#include "../FileSystem/SceneSaveFile.hpp"

namespace HexEngine
{
	class HCommand;

	class CommandManager : public IInputListener
	{
	public:		
		friend class HVar;
		friend class HCommand;

		void Create();
		~CommandManager();

		HVar* FindHVar(const std::string& name);
		std::vector<HVar*> FindHVars(const std::string& name);
		HCommand* FindHCommand(const std::string& name);

		void ProcessCommandInput(const std::string& command, bool pressed = true, void* param = nullptr);

		static void LockVars();
		static void UnlockVars();
		static void LockCommands();
		static void UnlockCommands();

		void CreateBind(int32_t key, const std::string& command, void* param=nullptr);
		void ProcessKeyInput(int32_t key, bool pressed);

		Console* GetConsole();

		// IInputListener
		virtual bool OnInputEvent(InputEvent event, InputData* data) override;

		void SaveVars(json& jsonFile);
		void LoadVars(json& jsonFile);

		void RegisterCommand(HCommand* command);
		void RegisterVar(HVar* var);

		int32_t GetNumVars() const { return (int32_t)_vars.size(); }

	private:
		static std::recursive_mutex _varLock;
		static std::recursive_mutex _cmdLock;
		std::map<int32_t, std::pair<std::string, void*>> _binds;
		std::vector<HCommand*> _commands;
		std::vector<HVar*> _vars;
		Console _console;

	public:
		
	};

	
}
