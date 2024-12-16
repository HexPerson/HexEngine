

#include "../HexEngine.Core/FileSystem/DiskFile.hpp"

namespace CityBuilder
{
	struct SaveFileHeader
	{
		int version;
		int numEnts;
	};

	class SaveFile : public HexEngine::DiskFile
	{
	public:
		SaveFile(const fs::path& absolutePath, std::ios_base::openmode openMode);

		const int Version = 1;

		bool Load();

		bool Save();
	};
}