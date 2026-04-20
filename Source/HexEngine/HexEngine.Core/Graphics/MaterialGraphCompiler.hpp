#pragma once

#include "../Required.hpp"
#include "MaterialGraph.hpp"
#include "MaterialGraphValidator.hpp"

namespace HexEngine
{
	class Material;

	struct MaterialGraphCompileResult
	{
		bool success = false;
		std::vector<std::string> errors;
		std::vector<std::string> warnings;
	};

	class HEX_API MaterialGraphCompiler
	{
	public:
		static MaterialGraphCompileResult CompileToMaterial(
			const MaterialGraph& graph,
			Material& material,
			const std::vector<MaterialGraphParameterOverride>* overrides = nullptr);

		static MaterialGraphCompileResult ApplyInstanceToMaterial(
			const MaterialGraph& graph,
			const MaterialGraphInstanceData& instanceData,
			Material& material);
	};
}
