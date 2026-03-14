
#pragma once

#include "../Required.hpp"

namespace HexEngine
{
	class Profiler
	{
	public:
		Profiler(const char* file, const char* function, uint32_t line);
		~Profiler();

		Profiler(const Profiler& other);

	public:
		float _start;
		float _end;
		float _average = 0.0f;
		int32_t _numProfiles = 0;
		char _file[256];
		char _func[128];
		uint32_t _line;
	};

#if 0//def _DEBUG
	#define PROFILE() Profiler __profile__##__LINE__(__FILE__, __FUNCTION__, __LINE__);
#else
	#define PROFILE()
#endif
}
