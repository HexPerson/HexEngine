

#include "AssetPacker.hpp"
#include <cxxopts.hpp>
#include <brotli\encode.h>
#include <cstring>

//HVar* HexEngine::g_hvars = nullptr;
//HCommand* HexEngine::g_commands = nullptr;
//int32_t HexEngine::g_numVars = 0;
//int32_t HexEngine::g_numCommands = 0;

int main(int argc, const char* argv[])
{
	cxxopts::Options options("AssetPacker", "A tool for creating asset packages from input resources");

	options.add_options()
		("I,input", "Input folder where assets are located to be packed", cxxopts::value<std::string>())
		("O,output", "Output file name", cxxopts::value<std::string>())
		("E,exclude", "Any file exclusions", cxxopts::value<std::string>())
		("C,compression", "Compression enabled", cxxopts::value<bool>());

	HexEngine::HeadlessOptions o;
	o.gameExtension = nullptr;
	// AssetPacker only needs the compression provider + DiskFile - no physics.
	// Asking for physics here would trigger a LOG_CRIT (and the MessageBox /
	// DebugBreak that comes with it) on machines whose runtime layout next to
	// the packer exe doesn't ship a physics plugin.
	o.requirePhysicsSystem = false;

	HexEngine::HeadlessEnvironment::Create(o);

	auto opts = options.parse(argc, argv);

	for (auto& kv : opts.arguments())
	{
		std::cout << kv.key() << " -> " << kv.value() << std::endl;
	}

	if (opts.count("input") == 0 || opts.count("output") == 0)
	{
		std::cerr << "Must have a valid input and output" << std::endl;
		std::cerr << options.help() << std::endl;
		return 1;
	}
	auto in = opts["input"].as<std::string>();
	auto out = opts["output"].as<std::string>();

	auto input = fs::weakly_canonical(in);
	auto output = fs::weakly_canonical(out);

	bool compress = opts["compression"].as<bool>();

	if (fs::exists(input) == false)
	{
		std::cerr << "The plugin folder does not exist" << std::endl;
		return 0;
	}

	fs::path inputPath = input;	
		
	if (fs::is_directory(inputPath) == false)
	{
		std::cerr << "The input path is not a directory" << std::endl;
		std::cerr << options.help() << std::endl;
		return 1;
	}

	// V2 format: write a header + per-file TOC + per-file compressed blobs.
	// Layout:
	//   [AssetFileHeader] (numFiles, tocSize)
	//   [AssetTocEntry × numFiles]      <- offsets point into the data section
	//   [data blob 1]                   <- offset = sizeof(header) + sizeof(toc)
	//   [data blob 2]                   <- offset = previous offset + blob 1's compressedSize
	//   ...
	// Each blob is independently Brotli-compressed (when compression enabled)
	// so the runtime can decode any one asset without touching the others,
	// which is what makes evict-then-reload cheap.

	struct PackerFileEntry
	{
		std::wstring relativePath;
		std::vector<uint8_t> diskBytes;          // bytes that go into the .pkg (compressed if isCompressed)
		uint32_t uncompressedSize = 0;
		bool isCompressed = false;
	};

	std::vector<PackerFileEntry> entries;

	for (auto const& dir_entry : std::filesystem::recursive_directory_iterator{ inputPath })
	{
		if (dir_entry.is_directory())
			continue;

		if (dir_entry.is_regular_file() == false)
			continue;

		const auto& path = dir_entry.path();

		if (path.string().find("\\Cache\\") != std::string::npos)
			continue;

		if (path.string().find(".pkg") != std::string::npos)
			continue;

		HexEngine::DiskFile diskFile(path, std::ios::binary | std::ios::in);

		if (diskFile.Open() == false)
			continue;

		const uint32_t fileSize = diskFile.GetSize();

		std::vector<uint8_t> raw(fileSize);
		diskFile.Read(raw.data(), fileSize);
		diskFile.Close();

		PackerFileEntry entry;
		entry.relativePath = fs::relative(path, inputPath).wstring();
		std::replace(entry.relativePath.begin(), entry.relativePath.end(), L'\\', L'/');
		entry.uncompressedSize = fileSize;

		if (compress)
		{
			std::vector<uint8_t> compressedBytes;
			if (HexEngine::g_pEnv->_compressionProvider->CompressData(raw, compressedBytes))
			{
				// Only adopt the compressed form when it actually saves space;
				// already-compressed assets (png, dds with DXTn, etc.) can
				// blow up under another pass of Brotli.
				if (compressedBytes.size() < raw.size())
				{
					entry.diskBytes = std::move(compressedBytes);
					entry.isCompressed = true;
				}
				else
				{
					entry.diskBytes = std::move(raw);
					entry.isCompressed = false;
				}
			}
			else
			{
				std::cerr << "Brotli compression failed for '" << path.string() << "', writing raw." << std::endl;
				entry.diskBytes = std::move(raw);
				entry.isCompressed = false;
			}
		}
		else
		{
			entry.diskBytes = std::move(raw);
			entry.isCompressed = false;
		}

		std::wcout << L"Adding file to archive: " << entry.relativePath
			<< L", uncompressed = " << std::dec << entry.uncompressedSize
			<< L", disk = " << entry.diskBytes.size()
			<< (entry.isCompressed ? L" [brotli]" : L" [raw]")
			<< std::endl;

		entries.push_back(std::move(entry));
	}

	if (entries.empty())
	{
		std::cerr << "No files were added to the archive" << std::endl;
		return 0;
	}

	std::cout << "Adding " << entries.size() << " files to archive (v2 streaming format):" << std::endl;

	HexEngine::AssetFileHeader header = {};
	header.version = HexEngine::AssetFileHeader::AssetVersion;  // 2
	header.numFiles = static_cast<uint16_t>(entries.size());
	header.compressed = false;  // V1-only field; ignored under V2 since compression is per-file
	header.tocSize = static_cast<uint32_t>(entries.size() * sizeof(HexEngine::AssetTocEntry));

	HexEngine::DiskFile outFile(output, std::ios::out | std::ios::binary, HexEngine::DiskFileOptions::CreateSubDirs);
	if (!outFile.Open())
		return 1;

	// Reserve space for header + TOC; we'll write actual TOC entries with
	// their final offsets after we know where each blob lands. For now,
	// just advance the write cursor past where the TOC will go.
	const uint64_t headerBytes = sizeof(HexEngine::AssetFileHeader);
	const uint64_t tocBytes = header.tocSize;
	const uint64_t dataStart = headerBytes + tocBytes;

	std::vector<HexEngine::AssetTocEntry> tocEntries(entries.size());

	// Compute offsets first (deterministic from the entry sizes, no I/O needed).
	uint64_t cursor = dataStart;
	for (size_t i = 0; i < entries.size(); ++i)
	{
		HexEngine::AssetTocEntry& toc = tocEntries[i];
		std::memset(&toc, 0, sizeof(toc));
		wcscpy_s(toc.relativePath, entries[i].relativePath.c_str());
		toc.offset = cursor;
		toc.compressedSize = static_cast<uint32_t>(entries[i].diskBytes.size());
		toc.uncompressedSize = entries[i].uncompressedSize;
		toc.isCompressed = entries[i].isCompressed ? 1u : 0u;

		cursor += entries[i].diskBytes.size();
	}

	// Header first, then the fully-populated TOC, then each blob in TOC order.
	outFile.Write(&header, sizeof(HexEngine::AssetFileHeader));
	outFile.Write(tocEntries.data(), static_cast<uint32_t>(tocEntries.size() * sizeof(HexEngine::AssetTocEntry)));

	for (auto& entry : entries)
	{
		outFile.Write(entry.diskBytes.data(), static_cast<uint32_t>(entry.diskBytes.size()));
	}

	outFile.Close();

	std::cout << "Successfully wrote v2 archive (per-file compression: "
		<< (compress ? "on" : "off") << ")" << std::endl;

	return 0;
}
