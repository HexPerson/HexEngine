

#include "BaseCompiler.hpp"


std::string gShaderStageToString[(uint32_t)ShaderStage::NumShaderStages] = {
	"VertexShader",
	"PixelShader",
	"GeometryShader",
	"HullShader",
	"DomainShader",
	"ComputeShader",
};

bool BaseCompiler::ReadShader(const fs::path& path, std::string shaderData[(uint32_t)ShaderStage::NumShaderStages], HexEngine::InputLayoutId& layoutId, HexEngine::ShaderRequirements& reqs, int lineOffsets[(uint32_t)ShaderStage::NumShaderStages])
{
	HexEngine::DiskFile file(path, std::ios::in);

	if (!file.Open())
	{
		printf("ShaderCompiler :: Failed to open '%S' for compilation!\n", path.c_str());
		return false;
	}

	HexEngine::KeyValues kv;

	if (!kv.Parse(&file))
	{
		printf("ShaderCompiler :: Failed to parse '%S'!\n", path.c_str());
		return false;
	}

	auto keyValues = kv.GetKeyValues();

	if (keyValues.size() == 0)
	{
		printf("ShaderCompiler :: Shader does not have any data or is not formatted correctly\n");
		return false;
	}
	

	if (auto inputLayout = keyValues.find("InputLayout"); inputLayout != keyValues.end())
	{		
		std::string layoutName = inputLayout->second.value;
		layoutName.pop_back();

		while(layoutName.at(0) == '\t' || layoutName.at(0) == ' ' || layoutName.at(0) == '\n')
			layoutName.erase(0,1);
		//std::getline(std::stringstream(inputLayout->second), layoutName);

		if (layoutName == "Pos")
			layoutId = HexEngine::InputLayoutId::Pos;
		else if (layoutName == "Pos_INSTANCED")
			layoutId = HexEngine::InputLayoutId::Pos_INSTANCED;
		else if (layoutName == "PosTex")
			layoutId = HexEngine::InputLayoutId::PosTex;
		else if (layoutName == "PosTex_INSTANCED")
			layoutId = HexEngine::InputLayoutId::PosTex_INSTANCED;
		else if (layoutName == "PosTex_INSTANCED_SIMPLE")
			layoutId = HexEngine::InputLayoutId::PosTex_INSTANCED_SIMPLE;
		else if (layoutName == "PosNormTanBinTex_INSTANCED")
			layoutId = HexEngine::InputLayoutId::PosNormTanBinTex_INSTANCED;
		else if (layoutName == "PosNormTanBinTexBoned_INSTANCED")
			layoutId = HexEngine::InputLayoutId::PosNormTanBinTexBoned_INSTANCED;
		else if (layoutName == "PosTexBoned_INSTANCED_SIMPLE")
			layoutId = HexEngine::InputLayoutId::PosTexBoned_INSTANCED_SIMPLE;
		else if (layoutName == "PosColour")
			layoutId = HexEngine::InputLayoutId::PosColour;
		else if (layoutName == "PosTexColour")
			layoutId = HexEngine::InputLayoutId::PosTexColour;
		else if (layoutName == "UI_INSTANCED")
			layoutId = HexEngine::InputLayoutId::UI_INSTANCED;

		//printf("Input layout is '%s' = %d\n", layoutName.c_str(), layoutId);
	}
	
	if (auto requirements = keyValues.find("Requirements"); requirements != keyValues.end())
	{
		std::stringstream requirementStream;
		requirementStream << requirements->second.value;

		std::string requirement;
		while (std::getline(requirementStream, requirement))
		{
			if (requirement.length() > 0)
			{
				requirement.erase(std::remove(requirement.begin(), requirement.end(), '\t'), requirement.end());

				//printf("Requires '%s'\n", requirement.c_str());

				if (requirement == "GBuffer")	reqs |= HexEngine::ShaderRequirements::RequiresGBuffer;
				if (requirement == "ShadowMaps")	reqs |= HexEngine::ShaderRequirements::RequiresShadowMaps;
				if (requirement == "Beauty")		reqs |= HexEngine::ShaderRequirements::RequiresBeauty;

				printf("Requirements = %X\n", reqs);
			}
		}
	}

	for (auto stage = 0U; stage < (uint32_t)ShaderStage::NumShaderStages; ++stage)
	{
		ProcessSection(gShaderStageToString[stage], keyValues, shaderData[stage], lineOffsets[stage]);
	}

	return true;
}

bool BaseCompiler::ProcessSection(const std::string& shaderStage, const HexEngine::KeyValues::KvMap& keyValues, std::string& mergedShaderData, int& lineOffset)
{
	std::string sectionName = shaderStage;

	auto shader = keyValues.find(sectionName);

	if (shader != keyValues.end())
	{
		lineOffset = shader->second.lineoffset;

		auto shaderData = shader->second.value;

		auto shaderIncludes = keyValues.find(sectionName + "Includes");

		if (shaderIncludes != keyValues.end())
		{
			std::stringstream includeSteam;
			includeSteam << shaderIncludes->second.value;

			std::string include;
			while (std::getline(includeSteam, include))
			{
				if (include.length() > 0)
				{
					include.erase(std::remove(include.begin(), include.end(), '\t'), include.end());

					std::string includeText = "#include \"" + include + ".shader\"\n";
					shaderData.insert(shaderData.begin(), includeText.begin(), includeText.end());
				}
			}
		}

		mergedShaderData = shaderData;

		return true;
	}

	return false;
}