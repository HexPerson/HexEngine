
#include "../Required.hpp"

#if ENABLE_MEMORY_LEAK_TRACKER == 1

typedef struct MemoryRecord_s
{
	//char			szFile[128];	//!< the file name where the memory was allocated
	char			szFunction[64]; //!< the function where the memory was allocated
	std::uint32_t	iLine;			//!< the line where the memory was allocated
	std::uint32_t	iSize;			//!< the size of the memory block that was allocated including this header (size of the leak)
	void* dwAddress;		//!< the address of the memory block (used for tracking)

} MemoryRecord_t;

const std::uint64_t MT_Signature = 0xCAFE0BABEB0000B5;  //!< this is the signature used to identify our memory blocks
bool g_bCritSecInitialised = false;
std::vector<MemoryRecord_t>	m_records;

#include "MemoryManager.hpp"

#pragma warning (disable : 4311)
#pragma warning (disable : 4312)
#pragma warning (disable : 4702)

void* xmalloc(size_t iSize);

namespace HexEngine
{
	

	

	MemoryTracker::MemoryTracker()
	{
		InitializeCriticalSection(&_cs);
		g_bCritSecInitialised = true;
		EnableMemoryLeakDetection();
	}

	MemoryTracker::~MemoryTracker()
	{
		DeleteCriticalSection(&_cs);
	}

	void* MemoryTracker::AllocateMemory(size_t iSize)
	{
		return AllocateMemory(iSize, 0, "undefined", "unknown function");
	}

	void* MemoryTracker::AllocateMemory(size_t iSize, std::uint32_t iLine, const char* szFile, const char* szFunction)
	{
		if (!g_bCritSecInitialised)
		{
			g_bCritSecInitialised = true;
			InitializeCriticalSection(&_cs);
		}
		// allocate enough room for our block of data
		void* p = malloc(iSize);

		if (p == 0)
		{
			//V5_ERROR(L"malloc failed (%d bytes)", iSize);
			return NULL;
		}

		MemoryRecord_t rec;

		// create the memory record
		rec.iLine = iLine;
		rec.iSize = (std::uint32_t)iSize;
		rec.dwAddress = p;
		//strcpy_s(rec.szFile, szFile);
		strcpy_s(rec.szFunction, szFunction);

		// add this memory to the list
		EnterCriticalSection(&_cs);
		m_records.push_back(rec);
		LeaveCriticalSection(&_cs);

		//char output[1024];
		//sprintf_s(output, "Added new memory allocation entry\nFile: %s\nLine: %d\nFunction: %s\n", szFile, iLine, szFunction);
		//OutputDebugString(output);

		return p;
	}

	void MemoryTracker::DeAllocateMemory(void* p)
	{
		if (!p)
			return;

		void* dwMemBlock = p;

		byte* pbData = (byte*)((std::uintptr_t)dwMemBlock - sizeof(MT_Signature));

		bool bIsSafeMemory = true;

		if (*(std::uint64_t*)pbData != MT_Signature)
			bIsSafeMemory = false;

		if (bIsSafeMemory)
		{
			RemoveRecord(dwMemBlock);

			(free)((void*)((std::uintptr_t)dwMemBlock - sizeof(MT_Signature)));
		}
		else
		{
			(free)(p);
		}
	}

	void MemoryTracker::RemoveRecord(void* dwAddress)
	{
		EnterCriticalSection(&_cs);

		for (unsigned int i = 0; i < m_records.size(); ++i)
		{
			MemoryRecord_t& pTmp = m_records.at(i);

			if (pTmp.dwAddress == dwAddress)
			{
				m_records.erase(m_records.begin() + i);
				LeaveCriticalSection(&_cs);
				return;
			}
		}
		LeaveCriticalSection(&_cs);
	}

	void MemoryTracker::DumpMemoryLeaks()
	{
		EnterCriticalSection(&_cs);

		for (unsigned int i = 0; i < m_records.size(); ++i)
		{
			MemoryRecord_t& pTmp = m_records.at(i);

			char szModule[256];
			GetModuleFileName(NULL, szModule, sizeof(szModule));

			std::string moduleName = szModule;

			auto p = moduleName.find_last_of('\\');

			if (p != moduleName.npos)
				moduleName = moduleName.substr(p + 1);

			char buf[1024];
			sprintf_s(buf, "**MEMORY LEAK** Func: %s Line: %d, Size: 0x%X\n", pTmp.szFunction, pTmp.iLine, pTmp.iSize);
			OutputDebugString(buf);

			//std::string fileName = pTmp.szFile;

			//p = fileName.rfind("\\source\\");

			//if (p != fileName.npos)
			//	fileName = "." + fileName.substr(p + 7);

			//V5_ERROR(L"Memory leak detected!\n\nModule:\t\t%S\nFile:\t\t%S\nLine:\t\t%d\nFunction:\t%S\nAddress:\t\t0x%p\nSize:\t\t%d", moduleName.c_str(), fileName.c_str(), pTmp.iLine, pTmp.szFunction, pTmp.dwAddress, pTmp.iSize);
		}

		if (m_records.size() > 0)
		{
			MessageBox(0, "Memory leaks were detected, check the output window for a detailed report!", "HexEngine Memory Manager", MB_ICONERROR | MB_TOPMOST);
		}

		LeaveCriticalSection(&_cs);
	}	
}

void* xmalloc(size_t iSize)
{
	// allocate enough space for the header too
	void* p = (malloc)(iSize + sizeof(MT_Signature));

	*(std::uint64_t*)p = MT_Signature;

	return (void*)((std::uintptr_t)p + sizeof(MT_Signature));
}

void xfree(void* p)
{
	HexEngine::gMemoryTracker.DeAllocateMemory(p);
}

//#undef new
//void* operator new(size_t size)
//{
//	return HexEngine::gMemoryTracker.AllocateMemory(size);
//}
//#define new HEX_NEW

#undef new
void* operator new[](size_t size)
{
	return HexEngine::gMemoryTracker.AllocateMemory(size);
}
#define new HEX_NEW

#undef new
void* operator new (size_t iSize, const char* szFile, int iLine, const char* szFunction)
{
	return HexEngine::gMemoryTracker.AllocateMemory(iSize, iLine, szFile, szFunction);
}
#define new HEX_NEW

#undef new
void* operator new[](size_t iSize, const char* szFile, int iLine, const char* szFunction)
{
	return HexEngine::gMemoryTracker.AllocateMemory(iSize, iLine, szFile, szFunction);
}
#define new HEX_NEW



void __cdecl operator delete[](void* ptr, char const* file, int line, const char* szFunction)
{
	HexEngine::gMemoryTracker.DeAllocateMemory(ptr);
}

void operator delete (void* p)
{
	HexEngine::gMemoryTracker.DeAllocateMemory(p);
}

void operator delete[](void* p)
{
	HexEngine::gMemoryTracker.DeAllocateMemory(p);
}

#pragma warning (default : 4702)
#pragma warning (default : 4311)
#pragma warning (default : 4312)

#endif // ENABLE_MEMORY_LEAK_TRACKER