
#pragma once

#include "DiskFile.hpp"

namespace HexEngine
{
	struct AssetHeader
	{
		wchar_t relativePath[128];
		uint32_t size;
		//uint8_t data[1];
	};

	struct AssetFileHeader
	{
		static const uint32_t AssetVersion = 1;

		uint16_t version;
		uint16_t numFiles;
		bool compressed;
	};

	class AssetPackage;

	class HEX_API AssetFile : public DiskFile
	{
	public:
		AssetFile(const fs::path& absolutePath);

		bool Unpack(std::shared_ptr<AssetPackage> package);

	private:
		AssetFileHeader* _header = nullptr;
		std::vector<uint8_t> _decompressed;
	};
}
