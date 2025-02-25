
#pragma once

#include "../Required.hpp"
#include "../Environment/IEnvironment.hpp"

namespace HexEngine
{


#define DECLARE_PLUGIN_INTERFACE(name, version) static inline const char* InterfaceName = #name#version;

	class IPluginInterface
	{
	public:
		virtual bool Create() = 0;

		virtual void Destroy() = 0;
	};

	class IPlugin
	{
	public:
		using tEntryFunc = IPlugin * (*)(IEnvironment*);
		using tExitFunc = void (*)(void);

		struct VersionData
		{
			VersionData() :
				majorVersion(0),
				minorVersion(0),
				enabled(true)
			{}

			int32_t majorVersion;
			int32_t minorVersion;
			std::string name;
			std::string description;
			std::string author;
			bool enabled;
		};

		//virtual bool Create() = 0;

		virtual void Destroy() = 0;

		virtual void GetVersionData(VersionData* data) = 0;

		virtual IPluginInterface* CreateInterface(const std::string& interfaceName) = 0;

		virtual void GetDependencies(std::vector<std::string>& dependencies) const = 0;
	};
	#define CREATE_PLUGIN(pointer, cls)	extern "C" __declspec(dllexport) IPlugin* CreatePlugin()\
	{\
		pointer = new cls;\
		return (IPlugin*)pointer;\
	}\
	extern "C" __declspec(dllexport) void DestroyPlugin()\
	{\
		SAFE_DELETE(pointer);\
	}
}
