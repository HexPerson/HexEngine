
#pragma once

#include "../FileSystem/DiskFile.hpp"
#include <Windows.h>
#include <mutex>
#include <string>
#include <vector>

namespace HexEngine
{
	enum class LogOptions
	{
		IncludeTime			= (1 << 0),
		AppendToExisting	= (1 << 1)
	};

	enum class LogLevel
	{
		Debug,
		Info,
		Warn,
		Crit
	};

	DEFINE_ENUM_FLAG_OPERATORS(LogOptions);
	DEFINE_ENUM_FLAG_OPERATORS(LogLevel);

	struct HEX_API LogMessage
	{
		LogLevel level = LogLevel::Info;
		std::string text;
		std::string file;
		std::string function;
		uint32_t line = 0;
	};

	class HEX_API ILogFileListener
	{
	public:
		virtual ~ILogFileListener() = default;
		virtual void OnLogMessage(const LogMessage& message) = 0;
	};

	class HEX_API LogFile : public DiskFile
	{
	public:
		LogFile(const fs::path& path, LogOptions options, LogLevel level);
		~LogFile();

		void Write(LogLevel level, const char* text, ...);

		void WriteLine(LogLevel level, const char* text, ...);

		void Write(LogLevel level, const char* file, const char* function, uint32_t line, const char* text, ...);

		void WriteLine(LogLevel level, const char* file, const char* function, uint32_t line, const char* text, ...);

		void AddListener(ILogFileListener* listener);
		void RemoveListener(ILogFileListener* listener);

	private:
		const char* ParseFileName(const char* file);
		void NotifyListeners(const LogMessage& message);

	private:
		LogOptions _options;
		LogLevel _level;
		std::vector<ILogFileListener*> _listeners;
		std::recursive_mutex _listenerLock;
	};


}
