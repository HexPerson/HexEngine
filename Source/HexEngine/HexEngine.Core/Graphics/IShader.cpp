

#include "IShader.hpp"
#include "../HexEngine.hpp"

namespace HexEngine
{
	void IShader::Destroy()
	{
		for (auto& stage : _stages)
		{
			if (stage != nullptr)
			{
				stage->Destroy();
				delete stage;
			}
		}
	}

	IShaderStage* IShader::GetShaderStage(ShaderStage stage)
	{
		return _stages[(uint32_t)stage];
	}

	IInputLayout* IShader::GetInputLayout() const
	{
		return _inputLayout;
	}

	ShaderRequirements IShader::GetRequirements() const
	{
		return _requirements;
	}

	IShader* IShader::GetDefaultShader()
	{
		static IShader* sDefaultShader = nullptr;

		if (!sDefaultShader)
		{
			sDefaultShader = (IShader*)g_pEnv->_resourceSystem->LoadResource("EngineData.Shaders/Default.hcs");
		}

		return sDefaultShader;
	}

	IShader* IShader::Create(const fs::path& path)
	{
		return (IShader*)g_pEnv->_resourceSystem->LoadResource(path);
	}
}