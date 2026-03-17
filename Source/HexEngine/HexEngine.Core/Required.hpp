
#pragma once

#if defined(_DEBUG) && !defined(NOMEMDBG)
	#define _CRTDBG_MAP_ALLOC
	#include <stdlib.h>
	#include <crtdbg.h>
#endif

#include <stdint.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <map>
#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>
#include <thread>
#include <filesystem>
#include <d3d11.h>
#include <unordered_map>
#include <future>
#include <bitset>
#include <set>
#include <list>
#include <cmath>
#include <array>
#include <hidusage.h>
#include <format>
#include <memory>
#include <deque>

#ifdef _DEBUG // for perf
	#include <d3d9.h>
	#pragma comment(lib, "d3d9.lib")
#endif

// from dxtk
//#include <GeometricPrimitive.h>
//#include <Keyboard.h>
//#include <Mouse.h>
#ifndef _XM_SSE4_INTRINSICS_
	#define _XM_SSE4_INTRINSICS_
#endif

#include <SimpleMath.h>
#include <nlohmann/json.hpp>
#include <rapidxml/rapidxml.hpp>

//#include "GUI\imgui\imgui.h"
//#include "GUI\imgui\backends\imgui_impl_win32.h"
//#include "GUI\imgui\backends\imgui_impl_dx11.h"

#include "Utility\MemoryManager.hpp"
#include "Utility\CRC32.hpp"

namespace fs = std::filesystem;
namespace math = DirectX::SimpleMath;
namespace dx = DirectX;
using json = nlohmann::json;

#pragma warning(disable: 4251)

#define SAFE_DELETE(x) if(x){ delete x; x = nullptr; }
#define SAFE_DELETE_ARRAY(x) if(x) { delete[] x; x = nullptr; }
#define SAFE_RELEASE(x) if(x) { x->Release(); x = nullptr; }
#define SAFE_UNLOAD(x) if(x) { HexEngine::g_pEnv->GetResourceSystem().UnloadResource(x); x = nullptr; }
#define SAFE_UNLOAD_ARRAY(x, s) if(x) { for(auto i = 0; i < s; ++i) { HexEngine::g_pEnv->_resourceSystem->UnloadResource(x[i]); x[i] = nullptr; } }

#define HEX_EXPORT __declspec(dllexport)
#define HEX_IMPORT __declspec(dllimport)

#ifdef HEX_CORE
#define HEX_API HEX_EXPORT
#else
#define HEX_API HEX_IMPORT
#endif

#define HEX_BITSET(x) (1 << x)
#define HEX_RGB_TO_FLOAT3(r,g,b) (float)r/255.0f, (float)g/255.0f, (float)b/255.0f
#define HEX_RGBA_TO_FLOAT4(r,g,b,a) (float)r/255.0f, (float)g/255.0f, (float)b/255.0f, (float)a/255.0f
#define HEX_RGBA(R,G,B,A)    (((uint32_t)(A)<<24) | ((uint32_t)(B)<<16) | ((uint32_t)(G)<<8) | ((uint32_t)(R)<<0))

#define MAKE_VERSION(tag, major, minor) inline const uint32_t tag = ((major << 4) | (minor << 0))

MAKE_VERSION(HexEngineVersion, 0, 1);
MAKE_VERSION(HexEditorVersion, 0, 1);

#define GET_MINOR_VERSION(version) (version & 0x0000000F)
#define GET_MAJOR_VERSION(version) ((version >> 4) & 0x0000000F)

#define HEX_HASFLAG(val,flag) (((uint32_t)val & (uint32_t)flag) != 0)
#define HEX_NOTHASFLAG(val,flag) (((uint32_t)val & (uint32_t)flag) == 0)

#define HEX_ASSERT(x) assert(x)

#define CHECK_HR(x) if(auto hr = x; FAILED(hr))\
{\
	LOG_CRIT(#x" failed: 0x%X", hr);\
}

static constexpr float ToRadian(float degree)
{
	return degree * 0.01745329f;
}

static constexpr float ToDegree(float radian)
{
	return radian * 57.29577951f;
}

#ifdef _DEBUG
	#define GFX_PERF_BEGIN(mask, name) D3DPERF_BeginEvent(mask, name)
	#define GFX_PERF_END() D3DPERF_EndEvent()
#else
	#define GFX_PERF_BEGIN(mask, name)
	#define GFX_PERF_END()
#endif

__forceinline std::wstring s2ws(const std::string& str)
{
	int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
	std::wstring wstrTo(size_needed, 0);
	MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
	return wstrTo;
}

__forceinline std::string ws2s(const std::wstring& str)
{
	int size_needed = WideCharToMultiByte(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0, NULL, NULL);
	std::string wstrTo(size_needed, 0);
	WideCharToMultiByte(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed, NULL, NULL);
	return wstrTo;
}