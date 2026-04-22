
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

	/** @brief Serialized header used by the engine shader binary format. */
	struct ShaderFileFormat
	{
		static const int32_t SHADER_FILE_VERSION = 1;

		int32_t _version;
		ShaderFileFlags _flags;
		InputLayoutId _inputLayout;
		ShaderRequirements _requirements;
		uint32_t _shaderSizes[(uint32_t)ShaderStage::NumShaderStages];
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
