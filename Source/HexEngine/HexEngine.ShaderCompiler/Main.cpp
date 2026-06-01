
#include "../HexEngine.Core/Required.hpp"
#include "../HexEngine.Core/Environment/IEnvironment.hpp"
#include "../HexEngine.Core/FileSystem/KeyValues.hpp"
#include <ShaderConductor/ShaderConductor.hpp>
#include <cxxopts.hpp>
#include "Compilers\BaseCompiler.hpp"
#include "Compilers\HLSL.hpp"

using namespace ShaderConductor;

fs::path gWorkingDirectory;

/**
 * @brief Writes a CompiledShader as a v2 .hcs file (multi-backend per stage).
 *
 * Output layout:
 *   ShaderFileFormat header (_version = SHADER_FILE_VERSION_V2, _flags has
 *     one bit per stage that has at least one blob, _shaderSizes is the
 *     total bytes of the stage body region).
 *   ShaderFileFormatV2Tail (_backendBitmap has one bit per ShaderBlobBackend
 *     ID that's present in that stage).
 *   For each present stage, in stage order:
 *     For each blob, in the order the compiler produced them:
 *       ShaderBlobHeader { _backendId, _blobBytes }
 *       uint8_t bytes[_blobBytes]
 *
 * The runtime loader (ShaderSystem::ParseShaderInternal) scans each stage's
 * body for the entry whose _backendId matches the active GraphicsBackend.
 */
static bool WriteShaderV2(const fs::path& outputPath, const CompiledShader& src)
{
	HexEngine::ShaderFileFormat header = {};
	header._version      = HexEngine::ShaderFileFormat::SHADER_FILE_VERSION_V2;
	header._flags        = (HexEngine::ShaderFileFlags)0;
	header._inputLayout  = src.inputLayout;
	header._requirements = src.requirements;

	HexEngine::ShaderFileFormatV2Tail tail = {};

	struct StageBuffer
	{
		std::vector<uint8_t> body;
		uint32_t             backendBitmap = 0;
	};
	StageBuffer stageBuffers[(uint32_t)HexEngine::ShaderStage::NumShaderStages];

	for (uint32_t s = 0; s < (uint32_t)HexEngine::ShaderStage::NumShaderStages; ++s)
	{
		const auto& cs = src.stages[s];
		if (cs.blobs.empty())
			continue;

		header._flags |= (HexEngine::ShaderFileFlags)HEX_BITSET((uint8_t)s);

		StageBuffer& sb = stageBuffers[s];
		for (const auto& blob : cs.blobs)
		{
			HexEngine::ShaderBlobHeader bh = {};
			bh._backendId = (uint32_t)blob.backend;
			bh._blobBytes = (uint32_t)blob.bytes.size();

			const uint8_t* bhBytes = reinterpret_cast<const uint8_t*>(&bh);
			sb.body.insert(sb.body.end(), bhBytes, bhBytes + sizeof(bh));
			sb.body.insert(sb.body.end(), blob.bytes.begin(), blob.bytes.end());

			sb.backendBitmap |= (1u << ((uint32_t)blob.backend - 1u)); // backend ids are 1-indexed
		}

		header._shaderSizes[s] = (uint32_t)sb.body.size();
		tail._backendBitmap[s] = sb.backendBitmap;
	}

	auto pathOnly = outputPath;
	pathOnly.remove_filename();
	fs::create_directories(pathOnly);

	HexEngine::DiskFile outFile(outputPath, std::ios::out | std::ios::binary | std::ios::trunc);
	if (!outFile.Open())
	{
		printf("ShaderCompiler :: Failed to open output '%S'\n", outputPath.c_str());
		return false;
	}

	outFile.Write(&header, sizeof(header));
	outFile.Write(&tail, sizeof(tail));

	for (uint32_t s = 0; s < (uint32_t)HexEngine::ShaderStage::NumShaderStages; ++s)
	{
		const StageBuffer& sb = stageBuffers[s];
		if (!sb.body.empty())
			outFile.Write((void*)sb.body.data(), (uint32_t)sb.body.size());
	}

	outFile.Close();
	printf("Wrote v2 shader '%S' (%d bytes)\n", outputPath.c_str(), outFile.GetSize());
	return true;
}

int main(int argc, const char* argv[])
{
	cxxopts::Options options("ShaderCompiler", "A tool for compiling HLSL to many shader languages.");

	options.add_options()
		("I,input", "Input file name", cxxopts::value<std::string>())
		("O,output", "Output file name", cxxopts::value<std::string>())
		("T,target", "Target shading language: hlsl (DXBC SM5 + DXIL SM6 multi-backend), dxbc-only (DXBC SM5 only)", cxxopts::value<std::string>()->default_value("hlsl"))
		("V,version", "The version of target shading language", cxxopts::value<std::string>()->default_value(""))
		("P,path", "Include path", cxxopts::value<std::string>()->default_value(""));

	auto opts = options.parse(argc, argv);

	if (opts.count("input") == 0 || opts.count("target") == 0)
	{
		std::cerr << "COULDN'T find <input> or <target> in command line parameters." << std::endl;
		std::cerr << options.help() << std::endl;
		return 1;
	}

	const auto input       = opts["input"].as<std::string>();
	const auto target      = opts["target"].as<std::string>();
	const auto output      = opts.count("output") > 0 ? opts["output"].as<std::string>() : "";
	const auto includePath = opts["path"].as<std::string>();

	printf("ShaderCompiler :: Compiling shader %s to target %s\nInclude path = %s\n", input.c_str(), target.c_str(), includePath.c_str());

	auto path = fs::path(input);

	gWorkingDirectory = path.parent_path();
	gWorkingDirectory += "/";

	BaseCompiler* compiler = nullptr;
	if (target == "hlsl" || target == "dxbc-only")
	{
		compiler = new HLSL;
	}
	if (compiler == nullptr)
	{
		printf("ShaderCompiler :: No supported compiler was found for target '%s'\n", target.c_str());
		return 1;
	}

	compiler->SetIncludePath(includePath);

	CompiledShader compiled;
	if (!compiler->Compile(input, compiled))
	{
		printf("ShaderCompiler :: Compile failed\n");
		delete compiler;
		return 1;
	}

	if (target == "dxbc-only")
	{
		// Strip the DXIL blobs for explicit-DXBC-only output. The v2 format
		// supports a single-backend bitmap fine; this option exists for
		// experimentation / diagnosing DXC-specific regressions.
		for (auto& stage : compiled.stages)
		{
			stage.blobs.erase(
				std::remove_if(stage.blobs.begin(), stage.blobs.end(),
					[](const CompiledStage::Blob& b)
					{
						return b.backend != HexEngine::ShaderBlobBackend::DXBC_SM5;
					}),
				stage.blobs.end());
		}
	}

	fs::path outputPath;
	if (!output.empty())
	{
		outputPath = output;
	}
	else
	{
		outputPath  = gWorkingDirectory;
		outputPath += "Compiled/";
		outputPath += path.stem();
		outputPath += ".hcs";
	}

	const bool ok = WriteShaderV2(outputPath, compiled);
	delete compiler;
	return ok ? EXIT_SUCCESS : 1;
}
