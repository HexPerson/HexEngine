
#pragma once

#include "DiskFile.hpp"

namespace HexEngine
{
	class XmlFile : public DiskFile
	{
	public:
		XmlFile(const fs::path& absolutePath, std::ios_base::openmode openMode, DiskFileOptions options = DiskFileOptions::None);
		XmlFile(const XmlFile& file) = delete;
	};
}
