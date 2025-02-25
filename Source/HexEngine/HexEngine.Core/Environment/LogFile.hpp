
#pragma once

#include "../FileSystem/DiskFile.hpp"
#include <Windows.h>

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

	class HEX_API LogFile : public DiskFile
	{
	public:
		LogFile(const fs::path& path, LogOptions options, LogLevel level);
		~LogFile();

		void Write(LogLevel level, const char* text, ...);

		void WriteLine(LogLevel level, const char* text, ...);

		void Write(LogLevel level, const char* file, const char* function, uint32_t line, const char* text, ...);

		void WriteLine(LogLevel level, const char* file, const char* function, uint32_t line, const char* text, ...);

	private:
		const char* ParseFileName(const char* file);

	private:
		LogOptions _options;
		LogLevel _level;
	};


}
