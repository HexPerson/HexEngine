
#pragma once

#include "../FileSystem/IResource.hpp"
#include "IShaderStage.hpp"
#include "IInputLayout.hpp"

namespace HexEngine
{
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

	enum class ShaderRequirements : uint32_t
	{
		None = 0,
		RequiresGBuffer = HEX_BITSET(0),
		RequiresShadowMaps = HEX_BITSET(1)
	};

	DEFINE_ENUM_FLAG_OPERATORS(ShaderRequirements);

	struct ShaderFileFormat
	{
		static const int32_t SHADER_FILE_VERSION = 1;

		int32_t _version;
		ShaderFileFlags _flags;
		InputLayoutId _inputLayout;
		ShaderRequirements _requirements;
		uint32_t _shaderSizes[(uint32_t)ShaderStage::NumShaderStages];
	};

	class IShader : public IResource
	{
		friend class ShaderSystem;

	public:
		static std::shared_ptr<IShader> Create(const fs::path& path);

		static std::shared_ptr<IShader> GetDefaultShader();

		virtual void Destroy() override;

		IShaderStage* GetShaderStage(ShaderStage stage);

		IInputLayout* GetInputLayout() const;

		ShaderRequirements GetRequirements() const;

		

	private:
		IShaderStage* _stages[(uint32_t)ShaderStage::NumShaderStages] = { nullptr };
		IInputLayout* _inputLayout = nullptr;
		ShaderRequirements _requirements = ShaderRequirements::None;
	};
}
