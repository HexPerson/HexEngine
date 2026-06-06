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

		// Re-runs HexEngine.ShaderCompiler.exe against the sibling .shader
		// source of `cachedHcsPath`, overwriting the .hcs. Used to rebake a
		// stale cache without needing the original MaterialGraph data - the
		// generated .shader source is persisted next to the .hcs at compile
		// time (see CompileToMaterial), so the runtime can recover from a
		// version / dialect mismatch (e.g. v1 DXBC under D3D12) by recompiling
		// straight from disk. Returns true iff the .hcs was successfully
		// regenerated. Errors are emitted via LOG_WARN; not appended to a
		// result vector because callers (ShaderSystem at load time) don't
		// have one.
		static bool TryRebakeCachedShader(const fs::path& cachedHcsPath);
	};
}
