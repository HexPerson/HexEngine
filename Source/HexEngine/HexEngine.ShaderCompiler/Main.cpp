
#include "../HexEngine.Core/Required.hpp"
#include "../HexEngine.Core/Environment/IEnvironment.hpp"
#include "../HexEngine.Core/FileSystem/KeyValues.hpp"
#include <ShaderConductor/ShaderConductor.hpp>
#include <cxxopts/cxxopts.hpp>
#include "Compilers\BaseCompiler.hpp"
#include "Compilers\HLSL.hpp"

//HVar* HexEngine::g_hvars = nullptr;
//HCommand* HexEngine::g_commands = nullptr;
//int32_t HexEngine::g_numVars = 0;
//int32_t HexEngine::g_numCommands = 0;

using namespace ShaderConductor;

bool ProcessShader(const char* filePath);
bool ProcessSection(const std::string& sectionName, const HexEngine::KeyValues::KvMap& keyValues, Blob* targetBlob);

fs::path gWorkingDirectory;



//Blob LoadInclude(const char* includeName)
//{
//	auto fullPath = gWorkingDirectory;
//	fullPath += std::string(includeName);
//
//	printf("Trying to load include file: %S\n", fullPath.c_str());
//
//	HexEngine::DiskFile file(fullPath, std::ios::in);
//
//	if (!file.Open())
//	{
//		printf("ShaderCompiler :: Failed to open '%S' for compilation!\n", fullPath.c_str());
//		return Blob();
//	}
//
//	HexEngine::KeyValues kv;
//
//	if (!kv.Parse(&file))
//	{
//		printf("ShaderCompiler :: Failed to parse '%S'!\n", fullPath.c_str());
//		return Blob();
//	}
//
//	auto keyValues = kv.GetKeyValues();
//
//	if (keyValues.size() == 0)
//	{
//		printf("ShaderCompiler :: Shader does not have any data or is not formatted correctly\n");
//		return Blob();
//	}
//
//	auto shader = keyValues.find("Global");
//
//	if (shader != keyValues.end())
//	{
//		auto shaderData = shader->second;
//
//		auto shaderIncludes = keyValues.find("GlobalIncludes");
//
//		if (shaderIncludes != keyValues.end())
//		{
//			std::stringstream includeSteam;
//			includeSteam << shaderIncludes->second;
//
//			if (shaderIncludes != keyValues.end())
//			{
//				std::string include;
//				while (std::getline(includeSteam, include))
//				{
//					if (include.length() > 0)
//					{
//						include.erase(std::remove(include.begin(), include.end(), '\t'), include.end());
//
//						std::string includeText = "#include \"" + include + ".shader\"\n";
//						shaderData.insert(shaderData.begin(), includeText.begin(), includeText.end());
//					}
//				}
//			}
//		}
//
//		return Blob(shaderData.data(), shaderData.length());
//	}
//
//	return Blob();	
//}
//
//Compiler::ResultDesc CompileShader(const std::string& data, ShaderStage stage)
//{
//	Compiler::SourceDesc sourceDesc{};
//	Compiler::TargetDesc targetDesc{};
//
//	sourceDesc.source = data.c_str();
//	sourceDesc.stage = stage;
//	sourceDesc.entryPoint = "ShaderMain";
//	sourceDesc.numDefines = 0;
//	sourceDesc.defines = nullptr;
//	sourceDesc.loadIncludeCallback = LoadInclude;
//
//	Compiler::Options opts;
//	opts.shaderModel.major_ver = 4;
//	opts.shaderModel.minor_ver = 0;
//
//	opts.optimizationLevel = 0;
//
//	targetDesc.asModule = false;
//	targetDesc.language = ShadingLanguage::Dxil;
//	targetDesc.version = nullptr;
//
//	auto result = Compiler::Compile(sourceDesc, {}, targetDesc);
//
//	return result;
//}
//
//struct DxilMinimalHeader
//{
//	UINT32 four_cc;
//	UINT32 hash_digest[4];
//};
//
//inline bool is_dxil_signed(void* buffer)
//{
//	DxilMinimalHeader* header = reinterpret_cast<DxilMinimalHeader*>(buffer);
//	bool has_digest = false;
//	has_digest |= header->hash_digest[0] != 0x0;
//	has_digest |= header->hash_digest[1] != 0x0;
//	has_digest |= header->hash_digest[2] != 0x0;
//	has_digest |= header->hash_digest[3] != 0x0;
//	return has_digest;
//}
//
//bool ProcessSection(ShaderStage stage, const HexEngine::KeyValues::KvMap& keyValues, Blob* targetBlob)
//{
//	std::string sectionName = gShaderStageToString[(uint32_t)stage];
//
//	auto shader = keyValues.find(sectionName);
//
//	if (shader != keyValues.end())
//	{
//		auto shaderData = shader->second;
//
//		auto shaderIncludes = keyValues.find(sectionName + "Includes");
//
//		if (shaderIncludes != keyValues.end())
//		{
//			std::stringstream includeSteam;
//			includeSteam << shaderIncludes->second;
//
//			if (shaderIncludes != keyValues.end())
//			{
//				std::string include;
//				while (std::getline(includeSteam, include))
//				{
//					if (include.length() > 0)
//					{
//						include.erase(std::remove(include.begin(), include.end(), '\t'), include.end());
//
//						std::string includeText = "#include \"" + include + ".shader\"\n";
//						shaderData.insert(shaderData.begin(), includeText.begin(), includeText.end());
//					}
//				}
//			}
//		}
//
//		auto result = CompileShader(shaderData, stage);
//
//		if (result.hasError)
//		{
//			printf("ShaderCompiler :: Error: '%s'\n", (const char*)result.errorWarningMsg.Data());
//			return false;
//		}
//		else
//		{
//			if (is_dxil_signed((void*)result.target.Data()) == false)
//			{
//				printf("ShaderCompiler :: Target is not DXIL signed!\n");
//				return false;
//			}
//			*targetBlob = result.target;
//			return true;
//		}
//	}
//	// we can still return true if the section wasn't found
//
//	return false;
//}

//bool ProcessShader(fs::path filePath)
//{
//	HexEngine::DiskFile file(filePath, std::ios::in);
//
//	if (!file.Open())
//	{
//		printf("ShaderCompiler :: Failed to open '%s' for compilation!\n", filePath);
//		return false;
//	}
//
//	HexEngine::KeyValues kv;
//
//	if (!kv.Parse(&file))
//	{
//		printf("ShaderCompiler :: Failed to parse '%s'!\n", filePath);
//		return false;
//	}
//
//	auto keyValues = kv.GetKeyValues();
//
//	if (keyValues.size() == 0)
//	{
//		printf("ShaderCompiler :: Shader does not have any data or is not formatted correctly\n");
//		return false;
//	}
//
//	HexEngine::ShaderFileFormat fileFormat = {};
//
//	std::vector<uint8_t> blobData;
//
//	Blob vertexShaderBlob;
//	if (ProcessSection(ShaderStage::VertexShader, keyValues, &vertexShaderBlob))
//	{
//		fileFormat._flags |= HexEngine::ShaderFileFlags::HasVertexShader;
//		fileFormat._vertexShaderSize = vertexShaderBlob.Size();
//		blobData.insert(blobData.end(), (uint8_t*)vertexShaderBlob.Data(), (uint8_t*)vertexShaderBlob.Data()+vertexShaderBlob.Size());
//	}
//
//#if 0
//	Blob pixelShaderBlob;
//	if (ProcessSection(ShaderStage::PixelShader, keyValues,  &pixelShaderBlob))
//	{
//		fileFormat._flags |= HexEngine::ShaderFileFlags::HasPixelShader;
//		fileFormat._pixelShaderSize = pixelShaderBlob.Size();
//	}
//
//	Blob geometryShaderBlob;
//	if (ProcessSection(ShaderStage::GeometryShader, keyValues, &geometryShaderBlob))
//	{
//		fileFormat._flags |= HexEngine::ShaderFileFlags::HasGeometryShader;
//		fileFormat._geometryShaderSize = geometryShaderBlob.Size();
//	}
//
//	Blob hullShaderBlob;
//	if (ProcessSection(ShaderStage::HullShader, keyValues, &hullShaderBlob))
//	{
//		fileFormat._flags |= HexEngine::ShaderFileFlags::HasHullShader;
//		fileFormat._hullShaderSize = hullShaderBlob.Size();
//	}
//
//	Blob domainShaderBlob;
//	if (ProcessSection(ShaderStage::DomainShader, keyValues, &domainShaderBlob))
//	{
//		fileFormat._flags |= HexEngine::ShaderFileFlags::HasDomainShader;
//		fileFormat._domainShaderSize = domainShaderBlob.Size();
//	}
//
//	Blob computeShaderBlob;
//	if (ProcessSection(ShaderStage::ComputeShader, keyValues, &computeShaderBlob))
//	{
//		fileFormat._flags |= HexEngine::ShaderFileFlags::HasComputeShader;
//		fileFormat._computeShaderSize = computeShaderBlob.Size();
//	}
//#endif
//
//	fs::path outputPath = gWorkingDirectory;
//	outputPath += "Compiled/";
//	outputPath += filePath.stem();
//	outputPath += ".hcs";
//
//	// Create the path if it doesn't exist
//	auto pathOnly = outputPath;
//	pathOnly.remove_filename();
//	fs::create_directories(pathOnly);
//
//	HexEngine::DiskFile outputShader(outputPath, std::ios::out | std::ios::binary | std::ios::trunc);
//
//	if (outputShader.Open())
//	{
//		// Write the header data
//		//
//		outputShader.Write(&fileFormat, sizeof(HexEngine::ShaderFileFormat));
//
//		// Write the blob data, if there is any
//		//
//		if (blobData.size() > 0)
//		{
//			outputShader.Write((void*)blobData.data(), blobData.size());
//		}
//
//		outputShader.Close();
//
//		printf("Successfully wrote compiled shader, final size is %d bytes\n", outputShader.GetSize());		
//	}
//
//	return true;
//}

int main(int argc, const char* argv[])
{
	cxxopts::Options options("ShaderCompiler", "A tool for compiling HLSL to many shader languages.");

	options.add_options()
		("I,input", "Input file name", cxxopts::value<std::string>())("O,output", "Output file name", cxxopts::value<std::string>())
		("T,target", "Target shading language: dxil, spirv, hlsl, glsl, essl, msl_macos, msl_ios", cxxopts::value<std::string>()->default_value("dxil"))
		("V,version", "The version of target shading language", cxxopts::value<std::string>()->default_value(""))
		("P,path", "Included", cxxopts::value<std::string>()->default_value(""));

	auto opts = options.parse(argc, argv);

	if (opts.count("input") == 0 || opts.count("target") == 0)
	{
		std::cerr << "COULDN'T find <input> or <target> in command line parameters." << std::endl;
		std::cerr << options.help() << std::endl;
		return 1;
	}

	const auto input = opts["input"].as<std::string>();
	const auto target = opts["target"].as<std::string>();
	const auto output = opts.count("output") > 0 ? opts["output"].as<std::string>() : "";
	const auto includePath = opts["path"].as<std::string>();

	printf("ShaderCompiler :: Compiling shader %s to target %s\nInclude path = %s\n", input.c_str(), target.c_str(), includePath.c_str());

	auto path = fs::path(input);

	gWorkingDirectory = path.parent_path();
	gWorkingDirectory += "/";

	BaseCompiler* compiler = nullptr;

	if (target == "hlsl")
	{
		compiler = new HLSL;
	}

	if (compiler == nullptr)
	{
		printf("ShaderCompiler :: No supported compiler was found\n");
		return 1;
	}


	//printf("ShaderCompiler :: Include path: %s\n", includePath.c_str());
	compiler->SetIncludePath(includePath);

	HexEngine::ShaderFileFormat shader = {};
	shader._version = HexEngine::ShaderFileFormat::SHADER_FILE_VERSION;

	std::vector<uint8_t> compiled;

	if(compiler->Compile(input, compiled, shader) && compiled.size() > 0)
	{
		fs::path outputPath;
		
		if (output.length() > 0)
		{
			outputPath = output;
		}
		else
		{
			outputPath = gWorkingDirectory;
			outputPath += "Compiled/";
			outputPath += path.stem();
			outputPath += ".hcs";
		}
	
		// Create the path if it doesn't exist
		auto pathOnly = outputPath;
		pathOnly.remove_filename();
		fs::create_directories(pathOnly);
	
		HexEngine::DiskFile outputShader(outputPath, std::ios::out | std::ios::binary | std::ios::trunc);
	
		if (outputShader.Open())
		{
			// Write the header data
			//
			outputShader.Write(&shader, sizeof(HexEngine::ShaderFileFormat));
	
			// Write the blob data, if there is any
			//
			outputShader.Write((void*)compiled.data(), (uint32_t)compiled.size());
	
			outputShader.Close();
	
			printf("Successfully wrote compiled shader, final size is %d bytes\n", outputShader.GetSize());		
		}
	}

	/*try
	{
		ProcessShader(fs::path(argv[1]));
	}
	catch (std::exception& e)
	{
		printf("ShaderCompiler :: %s\n", e.what());
	}*/

	return EXIT_SUCCESS;
}