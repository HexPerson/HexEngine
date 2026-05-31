
#pragma once

#include "../FileSystem/IResource.hpp"
#include "IShaderStage.hpp"
#include "IInputLayout.hpp"

namespace HexEngine
{
	/** @brief Shader stage identifiers used by compiled shader resources. */
	enum class ShaderStage : uint32_t
	{
		VertexShader,
		PixelShader,
		GeometryShader,
		HullShader,
		DomainShader,
		ComputeShader,
		NumShaderStages
	};

	/** @brief Bitmask flags indicating which shader stages are present in a shader file. */
	enum class ShaderFileFlags : uint32_t
	{
		HasVertexShader = HEX_BITSET(0),
		HasPixelShader = HEX_BITSET(1),
		HasGeometryShader = HEX_BITSET(2),
		HasHullShader = HEX_BITSET(3),
		HasDomainShader = HEX_BITSET(4),
		HasComputeShader = HEX_BITSET(5)
	};

	DEFINE_ENUM_FLAG_OPERATORS(ShaderFileFlags);

	/** @brief Resource binding requirements declared by a compiled shader. */
	enum class ShaderRequirements : uint32_t
	{
		None = 0,
		RequiresGBuffer		= HEX_BITSET(0),
		RequiresShadowMaps	= HEX_BITSET(1),
		RequiresBeauty		= HEX_BITSET(2)
	};

	DEFINE_ENUM_FLAG_OPERATORS(ShaderRequirements);

	/**
	 * @brief Identifies which compiled-shader bytecode dialect a blob is.
	 *
	 * V1 .hcs files implicitly hold one DXBC blob per stage (D3D11 / SM5).
	 * V2 files (Phase B addition) carry a per-blob backend tag so the same
	 * asset can ship DXBC + DXIL + SPIR-V side by side; the runtime loader
	 * picks whichever blob matches the active IGraphicsDevice's backend.
	 *
	 * Backend tags are stable serialised identifiers - do NOT renumber.
	 */
	enum class ShaderBlobBackend : uint32_t
	{
		DXBC_SM5 = 1, ///< D3D11 / FXC. The only payload v1 .hcs files contain.
		DXIL_SM6 = 2, ///< D3D12 / DXC. Reserved for the Phase B shader-compiler bring-up.
		SPIRV    = 3, ///< Future Vulkan backend.
	};

	/**
	 * @brief Serialized header used by the engine shader binary format.
	 *
	 * V1 layout (current; what HexEngine.ShaderCompiler emits today):
	 *   { _version=1, _flags, _inputLayout, _requirements,
	 *     _shaderSizes[6] }
	 *   followed by concatenated bytecode blobs in stage order, one blob per
	 *   stage that has its presence flag set. Each blob is implicitly DXBC.
	 *
	 * V2 layout (reserved; not yet emitted - bring-up planned in Phase B
	 * when the D3D12 plugin starts loading real shaders):
	 *   { _version=2, _flags, _inputLayout, _requirements,
	 *     _shaderSizes[6],
	 *     _backendBitmap[6] }   <-- new in v2, one bit per ShaderBlobBackend
	 *   followed, per present stage in stage order, by repeated:
	 *     { uint32_t backendId, uint32_t blobBytes, uint8_t blob[blobBytes] }
	 *   one entry per set bit in _backendBitmap[stage]. The runtime loader
	 *   picks the entry whose backendId matches the active backend (via
	 *   IGraphicsDevice::GetExpectedShaderBlobBackend()).
	 *
	 * Loader supports both versions; compiler currently emits v1 only.
	 */
	struct ShaderFileFormat
	{
		// Bumped when the asset layout changes in a way that requires
		// a re-bake. V1 = legacy single-DXBC layout. V2 = multi-backend.
		static const int32_t SHADER_FILE_VERSION    = 1;
		static const int32_t SHADER_FILE_VERSION_V2 = 2;

		int32_t _version;
		ShaderFileFlags _flags;
		InputLayoutId _inputLayout;
		ShaderRequirements _requirements;
		uint32_t _shaderSizes[(uint32_t)ShaderStage::NumShaderStages];
	};

	/** @brief Extension of ShaderFileFormat - only valid when _version == SHADER_FILE_VERSION_V2. */
	struct ShaderFileFormatV2Tail
	{
		uint32_t _backendBitmap[(uint32_t)ShaderStage::NumShaderStages]; ///< one bit per ShaderBlobBackend value
	};

	/** @brief Per-blob header used inside a v2 file body. */
	struct ShaderBlobHeader
	{
		uint32_t _backendId; ///< value cast from ShaderBlobBackend
		uint32_t _blobBytes; ///< size of the bytecode blob following this header
	};


	/** @brief Compiled multi-stage shader resource loaded by the shader system. */
	class HEX_API IShader : public IResource
	{
		friend class ShaderSystem;

	public:
		virtual ~IShader() override;

		/**
		 * @brief Loads a shader from an engine shader asset path.
		 * @param path Absolute or virtual asset path.
		 */
		static std::shared_ptr<IShader> Create(const fs::path& path);

		/** @brief Returns the default fallback shader resource. */
		static std::shared_ptr<IShader> GetDefaultShader();

		/** @brief Releases shader stages/input layout resources. */
		virtual void Destroy() override;

		/**
		 * @brief Returns a shader stage object if present.
		 * @param stage Stage type to query.
		 */
		IShaderStage* GetShaderStage(ShaderStage stage);

		/** @brief Returns the input layout associated with this shader. */
		IInputLayout* GetInputLayout() const;

		/** @brief Returns resource requirements declared by this shader. */
		ShaderRequirements GetRequirements() const;

		

	private:
		IShaderStage* _stages[(uint32_t)ShaderStage::NumShaderStages] = { nullptr };
		IInputLayout* _inputLayout = nullptr;
		ShaderRequirements _requirements = ShaderRequirements::None;
	};
}
