
#include "AssetFile.hpp"
#include "../Environment/IEnvironment.hpp"
#include "../Environment/LogFile.hpp"
#include "../FileSystem/ICompressionProvider.hpp"
#include "AssetPackage.hpp"

namespace HexEngine
{
	AssetFile::AssetFile(const fs::path& absolutePath) :
		DiskFile(absolutePath, std::ios::binary | std::ios::in)
	{}

	bool AssetFile::Unpack(std::shared_ptr<AssetPackage> package)
	{
		if (!Open())
			return false;

		std::vector<uint8_t> data, _decompressed;
		ReadAll(data);

		uint8_t* p = data.data();

		_header = reinterpret_cast<AssetFileHeader*>(data.data());

		if (_header->compressed)
		{
			if (g_pEnv->_compressionProvider->DecompressData(data, _decompressed) == false)
			{
				return false;
			}

			p = _decompressed.data();

			_header = reinterpret_cast<AssetFileHeader*>(p);
		}

		

		if (_header->version != AssetFileHeader::AssetVersion)
		{
			LOG_CRIT("Asset package has an incorrect version. Expected: %d, given: %d", AssetFileHeader::AssetVersion, _header->version);
			return false;
		}

		p += sizeof(AssetFileHeader);

		for (uint32_t i = 0; i < _header->numFiles; ++i)
		{
			AssetHeader* fileHeader = reinterpret_cast<AssetHeader*>(p);

			LOG_INFO("Asset: %S, size %d", fileHeader->relativePath, fileHeader->size);
			
			package->AddAsset(fileHeader);

			p += (sizeof(AssetHeader)+ fileHeader->size);
		}

		return true;
	}
}