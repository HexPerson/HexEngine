

#pragma once

#include "../../HexEngine.Core/Required.hpp"
#include "../../HexEngine.Core/Environment/IEnvironment.hpp"
#include "../../HexEngine.Core/FileSystem/KeyValues.hpp"
#include "../../HexEngine.Core/Graphics/IShader.hpp"
#include <ShaderConductor/ShaderConductor.hpp>

using namespace ShaderConductor;

extern fs::path gWorkingDirectory;

/**
 * @brief Per-stage compiled bytecode keyed by target backend.
 *
 * One CompiledStage per (engine) ShaderStage that appears in a .shader file.
 * The compiler tries every backend it knows how to produce; whichever succeed
 * land in `blobs`. Stages that are not present in the source leave the entry
 * empty (size() == 0).
 *
 * Serialised to disk as the v2 .hcs body: each blob becomes a {backendId,
 * blobBytes, payload} entry inside the stage's body region.
 */
struct CompiledStage
{
	struct Blob
	{
		HexEngine::ShaderBlobBackend backend = HexEngine::ShaderBlobBackend::DXBC_SM5;
		std::vector<uint8_t> bytes;
	};
	std::vector<Blob> blobs;
};

/** @brief Output of BaseCompiler::Compile - one CompiledStage per engine ShaderStage. */
struct CompiledShader
{
	CompiledStage stages[(uint32_t)HexEngine::ShaderStage::NumShaderStages];
	HexEngine::InputLayoutId    inputLayout   = (HexEngine::InputLayoutId)0;
	HexEngine::ShaderRequirements requirements = HexEngine::ShaderRequirements::None;
};

class BaseCompiler
{
public:
	virtual bool Compile(const fs::path& filePath, CompiledShader& out) = 0;

	void SetIncludePath(const std::string& path) { _includePath = path; }

protected:
	bool ReadShader(const fs::path& path, std::string shaderData[(uint32_t)ShaderStage::NumShaderStages], HexEngine::InputLayoutId& layoutId, HexEngine::ShaderRequirements& requirements, int lineOffsets[(uint32_t)ShaderStage::NumShaderStages]);
	bool ProcessSection(const std::string& shaderStage, const HexEngine::KeyValues::KvMap& keyValues, std::string& mergedShaderData, int& lineOffsets);

	std::string _includePath;
};
