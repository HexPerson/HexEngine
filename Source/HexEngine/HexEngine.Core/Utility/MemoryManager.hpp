

#pragma once

//#include "../Required.hpp"

#define ENABLE_MEMORY_LEAK_TRACKER 0

#if ENABLE_MEMORY_LEAK_TRACKER == 1

#define _MFC_OVERRIDES_NEW

#pragma warning (disable : 4291 )
#pragma warning (disable : 4005 ) // disable macro redefinition warning

namespace HexEngine
{
	class MemoryTracker
	{
	public:
		MemoryTracker();

		virtual ~MemoryTracker();

		void* AllocateMemory(size_t iSize);

		void* AllocateMemory(size_t iSize, std::uint32_t iLine, const char* szFile, const char* szFunction);

		void	DeAllocateMemory(void* p);

		void	RemoveRecord(void* p);

		void	DumpMemoryLeaks();

	private:

		CRITICAL_SECTION _cs;
	};

	inline MemoryTracker gMemoryTracker;
}

#if DISABLE_MEM_TRACKING == 0

//void* operator new  (size_t iSize);
void* operator new[](size_t iSize);
void* operator new  (size_t iSize, const char* szFile, int iLine, const char* szFunction);
void* operator new[](size_t iSize, const char* szFile, int iLine, const char* szFunction);
void  xfree(void* p);
void* xmalloc(size_t iSize);

// make global new our own new with file and line definititions
#define HEX_NEW new(__FILE__,__LINE__,__FUNCTION__)
#define new HEX_NEW

#define free(size) xfree(size)
#define malloc(size) xmalloc(size)

#endif

#ifndef EnableMemoryLeakDetection
	#define EnableMemoryLeakDetection() _CrtSetDbgFlag( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

#endif // ENABLE_MEMORY_LEAK_TRACKER