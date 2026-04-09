

#include "Profiler.hpp"
#include "../HexEngine.hpp"

namespace HexEngine
{
	Profiler::Profiler(const char* file, const char* function, uint32_t line)
	{
		_file = file;
		_func = function;
		_line = line;
		_average = 0.0f;
		_start = g_pEnv->_timeManager->GetTime();
	}

	Profiler::~Profiler()
	{
		_end = g_pEnv->_timeManager->GetTime();

		g_pEnv->_debugGui->ReportProfile(*this);
	}

	Profiler::Profiler(const Profiler& other)
	{
		_file = other._file;
		_func = other._func;
		_line = other._line;

		_start = other._start;
		_end = other._end;
		_average = other._average;
		_numProfiles = other._numProfiles;
	}
}