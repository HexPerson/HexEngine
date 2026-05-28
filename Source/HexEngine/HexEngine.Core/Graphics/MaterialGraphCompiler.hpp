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
		std::vector<std::pair<std::string, int32_t>> textureParameterSlots;
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

		// Returns true when the cached generated shader at `cachedShaderPath` was
		// built against a now-stale set of engine shader includes (Global.shader,
		// PBRutils.shader, etc). Used by MaterialLoader to decide whether a
		// graph-authored material's saved standard-shader path needs to be
		// recompiled at load time after engine shader changes. Compares the
		// includes-hash portion of the cached filename against ComputeIncludesHash
		// over the currently-resolvable include directory; mismatch = stale.
		// Filenames not matching the "_graph_<srcHash>_<incHash>.hcs" pattern
		// are treated as stale to be safe (better to recompile than to render
		// against a cached shader from an unknown era).
		static bool IsCachedGraphShaderStale(const fs::path& cachedShaderPath);
	};
}
