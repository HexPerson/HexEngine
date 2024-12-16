
#include "LogFile.hpp"
#include "../Input/Console.hpp"
#include "../Input/CommandManager.hpp"

namespace HexEngine
{
	LogFile::LogFile(const fs::path& path, LogOptions options, LogLevel level) :
		DiskFile(path,
			((options& LogOptions::AppendToExisting) == LogOptions::AppendToExisting ? std::ios::trunc : 0) | std::ios::out,
			DiskFileOptions::CreateSubDirs),
		_level(level),
		_options(options)
	{

		if (Open() == false)
		{
			// crit
		}
	}

	LogFile::~LogFile()
	{
		Close();
	}

	void LogFile::Write(LogLevel level, const char* text, ...)
	{
		if (IsOpen() == false)
			return;

		va_list	va_alist;

		va_start(va_alist, text);

		char buf[1024];

		_vsnprintf_s(buf, _TRUNCATE, text, va_alist);

		va_end(va_alist);

		std::stringstream ss;

		if (HEX_HASFLAG(_options, LogOptions::IncludeTime))
		{
			auto t = std::time(nullptr);
			tm tm;
			localtime_s(&tm, &t);

			ss << std::put_time(&tm, "[%T] ");
		}

		ss << buf;

		DiskFile::Write((void*)ss.str().c_str(), (uint32_t)ss.str().length());

		DiskFile::Flush();

		if(level != LogLevel::Debug)
			CON_ECHO(buf);

		if (level == LogLevel::Crit)
		{
			MessageBox(0, buf, "Critical Error", MB_ICONSTOP | MB_TOPMOST);
		}
	}

	void LogFile::Write(LogLevel level, const char* file, const char* function, uint32_t line, const char* text, ...)
	{
		if (IsOpen() == false)
			return;

		va_list	va_alist;

		va_start(va_alist, text);

		char buf[1024];

		_vsnprintf_s(buf, _TRUNCATE, text, va_alist);

		va_end(va_alist);

		std::stringstream ss;

		if (HEX_HASFLAG(_options, LogOptions::IncludeTime))
		{
			auto t = std::time(nullptr);
			tm tm;
			localtime_s(&tm, &t);

			ss << std::put_time(&tm, "[%T] ");
		}

		ss.setf(std::ios::left, std::ios::adjustfield);

		if (file)
		{
			ss.width(35);
			ss << ParseFileName(file) << " ";
		}

		if (function)
		{
			ss.width(60);
			ss << function << " ";
		}

		if (line > 0)
		{
			ss.width(8);
			ss << line << " ";
		}

		ss << buf;

		DiskFile::Write((void*)ss.str().c_str(), (uint32_t)ss.str().length());

		DiskFile::Flush();

		if (level != LogLevel::Debug)
			CON_ECHO(buf);

		if (level == LogLevel::Crit)
		{
			MessageBox(0, buf, "Critical Error", MB_ICONSTOP | MB_TOPMOST);
		}
	}

	void LogFile::WriteLine(LogLevel level, const char* text, ...)
	{
		if (IsOpen() == false)
			return;

		va_list	va_alist;

		va_start(va_alist, text);

		char buf[10240];

		_vsnprintf_s(buf, _TRUNCATE, text, va_alist);

		va_end(va_alist);

		std::stringstream ss;

		if (HEX_HASFLAG(_options, LogOptions::IncludeTime))
		{
			auto t = std::time(nullptr);
			tm tm;
			localtime_s(&tm, &t);

			ss << std::put_time(&tm, "[%T] ");
		}

		ss << buf << std::endl;

		DiskFile::Write((void*)ss.str().c_str(), (uint32_t)ss.str().length());

		DiskFile::Flush();

		if (level != LogLevel::Debug)
			CON_ECHO(buf);

		if (level == LogLevel::Crit)
		{
			MessageBox(0, buf, "Critical Error", MB_ICONSTOP | MB_TOPMOST);
		}
	}

	void LogFile::WriteLine(LogLevel level, const char* file, const char* function, uint32_t line, const char* text, ...)
	{
		if (IsOpen() == false)
			return;

		va_list	va_alist;

		va_start(va_alist, text);

		char buf[10240];

		_vsnprintf_s(buf, _TRUNCATE, text, va_alist);

		va_end(va_alist);

		std::stringstream ss;

		if (HEX_HASFLAG(_options, LogOptions::IncludeTime))
		{
			auto t = std::time(nullptr);
			tm tm;
			localtime_s(&tm, &t);

			ss << std::put_time(&tm, "[%T] ");
		}

		ss.setf(std::ios::left, std::ios::adjustfield);
		
		if (file)
		{
			ss.width(35);
			ss << ParseFileName(file) << " ";
		}

		if (function)
		{
			ss.width(60);
			ss << function << " ";
		}

		if (line > 0)
		{
			ss.width(8);
			ss << line << " ";
		}

		ss << buf << std::endl;

		DiskFile::Write((void*)ss.str().c_str(), (uint32_t)ss.str().length());

		DiskFile::Flush();

		if (level != LogLevel::Debug)
			CON_ECHO(buf);

		if (level == LogLevel::Crit)
		{
			MessageBox(0, buf, "Critical Error", MB_ICONSTOP | MB_TOPMOST);
#ifdef _DEBUG
			DebugBreak();
#endif
		}
	}

	const char* LogFile::ParseFileName(const char* file)
	{
		const char* p = strrchr(file, '\\');

		return p ? p+1 : file;
	}
}