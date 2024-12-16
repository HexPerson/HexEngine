

#include "AssetPacker.hpp"
#include <cxxopts/cxxopts.hpp>
#include <brotli\encode.h>


HexEngine::IEnvironment* HexEngine::g_pEnv = nullptr;

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

	HeadlessOptions o;
	o.gameExtension = nullptr;

	HeadlessEnvironment::Create(o);

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

	AssetFileHeader package;

	package.version = AssetFileHeader::AssetVersion;

#ifdef _DEBUG
	package.compressed = false;
#else
	package.compressed = true;
#endif
	
	std::vector<std::pair<AssetHeader, uint8_t*>> files;

	for (auto const& dir_entry : std::filesystem::recursive_directory_iterator{ inputPath })
	{
		if (dir_entry.is_directory())
			continue;

		if (dir_entry.is_regular_file() == false)
			continue;		

		const auto& path = dir_entry.path();

		if (path.string().find("AssetPackages") != std::string::npos)
			continue;

		

		DiskFile diskFile(path, std::ios::binary | std::ios::in);

		if (diskFile.Open() == false)
			continue;

		uint32_t fileSize = diskFile.GetSize();
		

		uint8_t* fileData = new uint8_t[fileSize];

		diskFile.Read(fileData, fileSize);

		std::pair<AssetHeader, uint8_t*> pair;

		AssetHeader& file = pair.first;
		file.size = fileSize;

		auto relativePath = fs::relative(path, inputPath/*g_pEnv->_fileSystem->GetDataDirectory()*/).wstring();

		std::replace(relativePath.begin(), relativePath.end(), '\\', '/');

		wcscpy_s(file.relativePath, relativePath.c_str());
		
		std::wcout << L"Adding file to archive: " << file.relativePath << L", size = " << std::hex << fileSize << std::endl;
		

		pair.second = fileData;
		files.push_back(pair);
	}

	if (files.size() == 0)
	{
		std::cerr << "No files were added to the archive" << std::endl;
		return 0;
	}

	std::cout << "Adding " << files.size() << " files to archive : " <<  std::endl;

	package.numFiles = (uint16_t)files.size();
	
	DiskFile outFile(output, std::ios::out | std::ios::binary, DiskFileOptions::CreateSubDirs);

	if (!outFile.Open())
		return 1;

	// Write the asset header
	//
	outFile.Write(&package, sizeof(AssetFileHeader));

	// Write all the files
	//
	for (auto& file : files)
	{
		// Write the header
		outFile.Write(&file.first , sizeof(AssetHeader));

		// Then follow with the data
		outFile.Write(file.second , file.first.size);

		delete[] file.second;
	}

	outFile.Close();

	if (package.compressed)
	{
		std::cout << "Compressing archive: " << output << std::endl;

		g_pEnv->_compressionProvider->CompressFile(output, output);

		std::cout << "Successfully wrote compressed archive" << std::endl;
	}

	return 0;
}