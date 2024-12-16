
#pragma once

#include "../Plugin/IPlugin.hpp"

namespace HexEngine
{
	class ICompressionProvider : public IPluginInterface
	{
	public:
		DECLARE_PLUGIN_INTERFACE(ICompressionProvider, 001);

		virtual bool CompressFile(const fs::path& absolutePath, const fs::path& output) = 0;

		virtual bool DecompressFile(const fs::path& absolutePath, const fs::path& output) = 0;

		virtual bool CompressFile(const fs::path& absolutePath, std::vector<uint8_t>& output) = 0;

		virtual bool DecompressFile(const fs::path& absolutePath, std::vector<uint8_t>& output) = 0;

		virtual bool CompressData(const std::vector<uint8_t>& data, std::vector<uint8_t>& output) = 0;

		virtual bool DecompressData(const std::vector<uint8_t>& data, std::vector<uint8_t>& output) = 0;
	};
}
