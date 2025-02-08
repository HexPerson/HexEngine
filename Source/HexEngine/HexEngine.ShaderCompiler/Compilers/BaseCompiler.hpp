

#pragma once

#include "../../HexEngine.Core/Required.hpp"
#include "../../HexEngine.Core/Environment/IEnvironment.hpp"
#include "../../HexEngine.Core/FileSystem/KeyValues.hpp"
#include "../../HexEngine.Core/Graphics/IShader.hpp"
#include <ShaderConductor/ShaderConductor.hpp>

using namespace ShaderConductor;

extern fs::path gWorkingDirectory;

class BaseCompiler
{
public:
	virtual bool Compile(const fs::path& filePath, std::vector<uint8_t>& dataOut, HexEngine::ShaderFileFormat& shader) = 0;

	void SetIncludePath(const std::string& path) { _includePath = path; }

protected:
	bool ReadShader(const fs::path& path, std::string shaderData[(uint32_t)ShaderStage::NumShaderStages], HexEngine::InputLayoutId& layoutId, HexEngine::ShaderRequirements& requirements, int lineOffsets[(uint32_t)ShaderStage::NumShaderStages]);
	bool ProcessSection(const std::string& shaderStage, const HexEngine::KeyValues::KvMap& keyValues, std::string& mergedShaderData, int& lineOffsets);

	std::string _includePath;
};
