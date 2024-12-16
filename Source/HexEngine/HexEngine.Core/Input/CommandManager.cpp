

#include "CommandManager.hpp"
#include "../Environment/IEnvironment.hpp"
#include "../Environment/LogFile.hpp"
#include "../Scene/SceneManager.hpp"
#include "HCommand.hpp"

namespace HexEngine
{
	std::recursive_mutex CommandManager::_varLock;
	std::recursive_mutex CommandManager::_cmdLock;

	HEX_COMMAND(bind)
	{
		if (pressed)
		{
			if (args->Count() < 2)
			{
				CON_ECHO("^r'bind' usage: ^ybind <key> <command(s)>");
				return;
			}

			int32_t key = args->Get<int32_t>(0);

			std::string commandStr;
			for (auto i = 1; i < args->GetAll().size(); ++i)
			{
				commandStr.append(args->GetAll()[i]);

				if (i < args->GetAll().size() - 1)
					commandStr.append(" ");
			}

			g_pEnv->_commandManager->CreateBind(key, commandStr);
		}
	}

	HEX_COMMAND(echo)
	{
		if (pressed)
		{
			if (args->Count() < 1)
			{
				CON_ECHO("^r'echo' usage: ^yecho <text>");
				return;
			}

			std::string commandStr;
			for (auto i = 0; i < args->GetAll().size(); ++i)
			{
				commandStr.append(args->GetAll()[i]);

				if (i < args->GetAll().size() - 1)
					commandStr.append(" ");
			}

			if (commandStr.length() > 0)
				g_pEnv->_commandManager->GetConsole()->Print("%s", commandStr.c_str());
		}
	}

	HEX_COMMAND(set_entity_pos)
	{
		if (pressed)
		{
			if (args->Count() < 4)
			{
				CON_ECHO("^r'set_entity_pos' usage: ^yset_entity_pos <entity_name> x y z");
				return;
			}

			std::string entityName = args->Get<std::string>(0);
			float xpos = args->Get<float>(1);
			float ypos = args->Get<float>(2);
			float zpos = args->Get<float>(3);

			if (auto entity = g_pEnv->_sceneManager->GetCurrentScene()->GetEntityByName(entityName); entity != nullptr)
			{
				entity->SetPosition(math::Vector3(xpos, ypos, zpos));

				CON_ECHO("^gEntity '^y%s^g' was moved to ^y%f %f %f", entityName.c_str(), xpos, ypos, zpos);
			}
		}
	}

	HEX_COMMAND(set_entity_rot)
	{
		if (pressed)
		{
			if (args->Count() < 4)
			{
				CON_ECHO("^r'set_entity_rot' usage: ^yset_entity_rot <entity_name> x y z");
				return;
			}

			std::string entityName = args->Get<std::string>(0);
			float xrot = args->Get<float>(1);
			float yrot = args->Get<float>(2);
			float zrot = args->Get<float>(3);

			if (auto entity = g_pEnv->_sceneManager->GetCurrentScene()->GetEntityByName(entityName); entity != nullptr)
			{
				entity->SetRotation(math::Quaternion::CreateFromYawPitchRoll(ToRadian(yrot), ToRadian(xrot), ToRadian(zrot)));

				CON_ECHO("^gEntity '^y%s^g' was rotated to ^y%f %f %f", entityName.c_str(), xrot, yrot, zrot);
			}
		}
	}

	void CommandManager::Create()
	{
		g_pEnv->_inputSystem->AddInputListener(this, InputEvent::KeyDown | InputEvent::MouseDown | InputEvent::KeyUp | InputEvent::MouseUp | InputEvent::Char);

		CON_ECHO("^rred ^ggreen ^bblue");

		RegisterCommand(&cmd_bind);
		RegisterCommand(&cmd_echo);
		RegisterCommand(&cmd_set_entity_pos);
		RegisterCommand(&cmd_set_entity_rot);
	}

	CommandManager::~CommandManager()
	{
		_console.Destroy();

		g_pEnv->_inputSystem->RemoveInputListener(this);
	}

	void CommandManager::RegisterCommand(HCommand* command)
	{
		if (FindHCommand(command->_name))
			return;

		_commands.push_back(command);
	}

	void CommandManager::RegisterVar(HVar* var)
	{
		if (FindHVar(var->_name))
			return;

		_vars.push_back(var);
	}

	HVar* CommandManager::FindHVar(const std::string& name)
	{
		const std::lock_guard<std::recursive_mutex> lock(_varLock);

		for(auto& var : _vars)
		{
			if (var->_name == name)
				return var;
		}

		return nullptr;
	}

	std::vector<HVar*> CommandManager::FindHVars(const std::string& name)
	{
		const std::lock_guard<std::recursive_mutex> lock(_varLock);

		std::vector<HVar*> vars;

		for (auto& var : _vars)
		{
			if (var->_name.find(name) != name.npos)
			{
				vars.push_back(var);
			}
		}

		return vars;
	}

	HCommand* CommandManager::FindHCommand(const std::string& name)
	{
		const std::lock_guard<std::recursive_mutex> lock(_cmdLock);

		for(auto& cmd : _commands)
		{
			if (cmd->_name == name)
				return cmd;

			//cmd = cmd->_next;
		}

		return nullptr;
	}

	/*HVar* CommandManager::GetVars()
	{
		return GetVars();
	}

	HCommand* CommandManager::GetCommands()
	{
		return GetCommands();
	}*/

	void CommandManager::LockVars()
	{
		//_varLock.lock();
	}

	void CommandManager::UnlockVars()
	{
		//_varLock.unlock();
	}

	void CommandManager::LockCommands()
	{
		//_cmdLock.lock();
	}

	void CommandManager::UnlockCommands()
	{
		//_cmdLock.unlock();
	}

	void CommandManager::ProcessCommandInput(const std::string& command, bool pressed, void* param)
	{
		// split into tokens
		auto tokenize = [](std::string str, const std::string& token)
		{
			std::vector<std::string> result;
			while (str.size()) {
				size_t index = str.find(token);
				if (index != std::string::npos) {

					auto tok = str.substr(0, index);
					if(tok.length() > 0)
						result.push_back(tok);

					str = str.substr(index + token.size());
					//if (str.size() == 0)result.push_back(str);
				}
				else {
					result.push_back(str);
					str = "";
				}
			}
			return result;
		};

		std::vector<std::string> tokens = tokenize(command, " ");

		if (tokens.size() == 0)
			return;

		// check for commands first
		auto pCommand = FindHCommand(tokens[0]);

		if (pCommand)
		{
			CommandArgs args;
			args._args.insert(args._args.end(), tokens.begin() + 1, tokens.end());

			pCommand->_func(&args, pressed, param);
			return;
		}

		// then process hvars
		auto pVar = FindHVar(tokens[0]);

		if (pVar)
		{
			if (tokens.size() > 1)
			{
				switch (pVar->GetType())
				{
				case HVar::Type::Bool:
				{
					bool old = pVar->_val.b;
					pVar->_val.b = std::stoi(tokens[1]) == 1;
					pVar->Clamp();
					CON_ECHO("^gHVar '%s' change from %d to %d", pVar->_name.c_str(), old, pVar->_val.b);
					break;
				}
				case HVar::Type::Float32:
				{
					float old = pVar->_val.f32;
					pVar->_val.f32 = std::stof(tokens[1]);
					pVar->Clamp();
					CON_ECHO("^gHVar '%s' change from %f to %f", pVar->_name.c_str(), old, pVar->_val.f32);
					break;
				}
				case HVar::Type::Float64:
				{
					double old = pVar->_val.f64;
					pVar->_val.f64 = std::stod(tokens[1]);
					pVar->Clamp();
					CON_ECHO("^gHVar '%s' change from %f to %f", pVar->_name.c_str(), old, pVar->_val.f64);
					break;
				}
				case HVar::Type::Int8:
				{
					int8_t old = pVar->_val.i8;
					pVar->_val.i8 = (int8_t)std::stoi(tokens[1]);
					pVar->Clamp();
					CON_ECHO("^gHVar '%s' change from %hhd to %hhd", pVar->_name.c_str(), old, pVar->_val.i8);
					break;
				}
				case HVar::Type::Int16:
				{
					int16_t old = pVar->_val.i16;
					pVar->_val.i16 = (int16_t)std::stoi(tokens[1]);
					pVar->Clamp();
					CON_ECHO("^gHVar '%s' change from %hd to %hd", pVar->_name.c_str(), old, pVar->_val.i16);
					break;
				}
				case HVar::Type::Int32:
				{
					int32_t old = pVar->_val.i32;
					pVar->_val.i32 = std::stoi(tokens[1]);
					pVar->Clamp();
					CON_ECHO("^gHVar '%s' change from %d to %d", pVar->_name.c_str(), old, pVar->_val.i32);
					break;
				}
				case HVar::Type::Int64:
				{
					int64_t old = pVar->_val.i64;
					pVar->_val.i64 = std::stoll(tokens[1]);
					pVar->Clamp();
					CON_ECHO("^gHVar '%s' change from %lld to %lld", pVar->_name.c_str(), old, pVar->_val.i64);
					break;
				}
				case HVar::Type::UInt8:
				{
					uint8_t old = pVar->_val.ui8;
					pVar->_val.ui8 = (uint8_t)std::stoul(tokens[1]);
					pVar->Clamp();
					CON_ECHO("^gHVar '%s' change from %hhu to %hhu", pVar->_name.c_str(), old, pVar->_val.ui8);
					break;
				}
				case HVar::Type::UInt16:
				{
					uint16_t old = pVar->_val.ui16;
					pVar->_val.ui16 = (uint16_t)std::stoul(tokens[1]);
					pVar->Clamp();
					CON_ECHO("^gHVar '%s' change from %hu to %hu", pVar->_name.c_str(), old, pVar->_val.ui16);
					break;
				}
				case HVar::Type::UInt32:
				{
					uint32_t old = pVar->_val.ui32;
					pVar->_val.ui32 = std::stoul(tokens[1]);
					pVar->Clamp();
					CON_ECHO("^gHVar '%s' change from %u to %u", pVar->_name.c_str(), old, pVar->_val.ui32);
					break;
				}
				case HVar::Type::UInt64:
				{
					uint64_t old = pVar->_val.ui64;
					pVar->_val.ui64 = std::stoull(tokens[1]);
					pVar->Clamp();
					CON_ECHO("^gHVar '%s' change from %llu to %llu", pVar->_name.c_str(), old, pVar->_val.ui64);
					break;
				}

				case HVar::Type::Vector3:
				{
					math::Vector3 old = pVar->_val.v3;
					pVar->_val.v3.x = std::stoull(tokens[1]);
					pVar->_val.v3.y = std::stoull(tokens[2]);
					pVar->_val.v3.z = std::stoull(tokens[3]);
					pVar->Clamp();
					CON_ECHO("^gHVar '%s' change from %f %f %f to %f %f %f", pVar->_name.c_str(),
						old.x, old.y, old.z, 
						pVar->_val.v3.x, pVar->_val.v3.y, pVar->_val.v3.z);
					break;
				}
				}

				return;
			}
			else
			{
				switch (pVar->GetType())
				{
				case HVar::Type::Bool:
				{
					CON_ECHO("^g'%s' ^y= ^w%hhd", pVar->_name.c_str(), pVar->_val.b);
					break;
				}
				case HVar::Type::Float32:
				{
					CON_ECHO("^g'%s' ^y= ^w%f", pVar->_name.c_str(), pVar->_val.f32);
					break;
				}
				case HVar::Type::Float64:
				{
					CON_ECHO("^g'%s' ^y= ^w%f", pVar->_name.c_str(), pVar->_val.f64);
					break;
				}
				case HVar::Type::Int8:
				{
					CON_ECHO("^g'%s' ^y= ^w%hhd", pVar->_name.c_str(), pVar->_val.i8);
					break;
				}
				case HVar::Type::Int16:
				{
					CON_ECHO("^g'%s' ^y= ^w%hd", pVar->_name.c_str(), pVar->_val.i16);
					break;
				}
				case HVar::Type::Int32:
				{
					CON_ECHO("^g'%s' ^y= ^w%d", pVar->_name.c_str(), pVar->_val.i32);
					break;
				}
				case HVar::Type::Int64:
				{
					CON_ECHO("^g'%s' ^y= ^w%lld", pVar->_name.c_str(), pVar->_val.i64);
					break;
				}
				case HVar::Type::UInt8:
				{
					CON_ECHO("^g'%s' ^y= ^w%hhu", pVar->_name.c_str(), pVar->_val.ui8);
					break;
				}
				case HVar::Type::UInt16:
				{
					CON_ECHO("^g'%s' ^y= ^w%hu", pVar->_name.c_str(), pVar->_val.ui16);
					break;
				}
				case HVar::Type::UInt32:
				{
					CON_ECHO("^g'%s' ^y= ^w%u", pVar->_name.c_str(), pVar->_val.ui32);
					break;
				}
				case HVar::Type::UInt64:
				{
					CON_ECHO("^g'%s' ^y= ^w%llu", pVar->_name.c_str(), pVar->_val.ui64);
					break;
				}
				}
				return;
			}
		}

		CON_ECHO("^rUnrecognised command or variable: ^y'%s'", command.c_str());
	}

	void CommandManager::CreateBind(int32_t key, const std::string& command, void* param)
	{
		auto existingBind = _binds.find(key);

		if (existingBind != _binds.end())
		{
			LOG_WARN("Attempting to bind a key which is already bound, unbind the key first then rebind");
			return;
		}

		_binds[key] = std::make_pair(command, param);
	}

	void CommandManager::ProcessKeyInput(int32_t key, bool pressed)
	{
		auto existingBind = _binds.find(key);

		if (existingBind == _binds.end())
		{
			return;
		}

		ProcessCommandInput(existingBind->second.first, pressed, existingBind->second.second);
	}

	Console* CommandManager::GetConsole()
	{
		return &_console;
	}

	bool CommandManager::OnInputEvent(InputEvent event, InputData* data)
	{
		if (event == InputEvent::KeyDown || event == InputEvent::KeyUp)
		{
			if (_console.GetActive())
				_console.OnKeyInput(data->KeyDown.key, event == InputEvent::KeyDown);

			ProcessKeyInput(data->KeyDown.key, event == InputEvent::KeyDown);

			return _console.GetActive();
		}
		else if (event == InputEvent::Char && _console.GetActive())
		{
			_console.OnCharInput(data->Char.ch);
			return true;
		}

		return false;
	}

	void CommandManager::SaveVars(json& jsonFile)
	{
		//header->numVars = _vars.size();

		for(auto& var : _vars)
		{
			jsonFile[var->_name] = var->ValToString();
			//file->WriteString(var->_name);
			//file->WriteString(var->ValToString());
		}
	}

	void CommandManager::LoadVars(json& jsonFile)
	{
		for (auto& var : jsonFile.items())
		{
			auto varName = var.key();
			auto varValue = var.value().get<std::string>();

			ProcessCommandInput(varName + " " + varValue);
		}
		/*for (int32_t i = 0; i < jsonFile["header"]["numVars"]; ++i)
		{
			auto varName = file->ReadString();
			auto varValue = file->ReadString();

			ProcessCommandInput(varName + " " + varValue);
		}*/
	}

	/*int32_t CommandManager::GetNumVars()
	{ 
		return GetNumVars();
	}*/
}