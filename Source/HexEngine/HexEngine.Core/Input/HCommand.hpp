
#pragma once

#include "../Required.hpp"
#include "Hvar.hpp"
#include "CommandManager.hpp"

namespace HexEngine
{
	

	class HEX_API CommandArgs
	{
	public:
		friend class CommandManager;

		template <typename T>
		T Get(int32_t idx) const
		{
			return _args.at(idx);
		}

		template <>
		int32_t Get(int32_t idx) const
		{
			return std::stoi(_args.at(idx));
		}

		template <>
		uint32_t Get(int32_t idx) const
		{
			return std::stoul(_args.at(idx));
		}

		template <>
		float Get(int32_t idx) const
		{
			return std::stof(_args.at(idx));
		}

		template <>
		bool Get(int32_t idx) const
		{
			return std::stoi(_args.at(idx)) == 1;
		}

		template <>
		double Get(int32_t idx) const
		{
			return std::stod(_args.at(idx));
		}

		size_t Count() const
		{
			return _args.size();
		}

		const std::vector<std::string>& GetAll() const
		{
			return _args;
		}

	private:
		std::vector<std::string> _args;
	};

	using CommandFunc = std::function<void(CommandArgs*, bool, void*)>;

	class HEX_API HCommand
	{
	public:
		HCommand(const char* name, CommandFunc func);

	public:
		std::string _name;
		HCommand* _next;
		CommandFunc _func;
	};

	extern HEX_API HCommand* g_commands;
	extern HEX_API uint32_t g_numCommands;

#define HEX_COMMAND(funcname)\
void funcname##_fn(CommandArgs* args, bool pressed, void* param);\
HCommand cmd_##funcname(#funcname, funcname##_fn);\
void funcname##_fn(CommandArgs* args, bool pressed, void* param)	
}
