

#pragma once

#include <HexEngine.Core/Graphics/IShaderStage.hpp>

template<typename T>
class ShaderStageImpl : public HexEngine::IShaderStage
{
	friend class GraphicsDeviceD3D11;

public:
	virtual void Destroy() override
	{
		SAFE_RELEASE(_shader);
	}

	virtual void* GetNativePtr() override
	{
		return _shader;
	}

	virtual bool GetBinaryCode(std::vector<uint8_t>& code) override
	{
		code = _shaderCode;
		return code.size() > 0;
	}

	virtual void CopyFrom(IShaderStage* other) override
	{
		auto otherImpl = (ShaderStageImpl<T>*)other;

		_shaderCode = otherImpl->_shaderCode;
		_shader = otherImpl->_shader;
	}

private:
	std::vector<uint8_t> _shaderCode;
	/*ID3D11VertexShader* _vertexShader = nullptr;
	ID3D11PixelShader* _pixelShader = nullptr;
	ID3D11GeometryShader* _geometryShader = nullptr;
	ID3D11HullShader* _hullShader = nullptr;
	ID3D11DomainShader* _domainShader = nullptr;
	ID3D11ComputeShader* _computeShader = nullptr;*/
	T* _shader = nullptr;
};
