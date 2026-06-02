
#pragma once

#include <HexEngine.Core/Graphics/IShaderStage.hpp>
#include <vector>

/**
 * @brief D3D12 shader stage - just a bytecode container.
 *
 * Unlike D3D11 where each compiled stage is its own ID3D11*Shader COM
 * object, D3D12 only consumes the bytecode at PSO creation time. So a
 * "stage" here is literally just the DXIL blob.
 *
 * PSO cache (Phase B4) hashes the blob pointer + per-stage metadata to
 * key its lookup, then forwards the bytes into D3D12_SHADER_BYTECODE
 * fields on the PSO desc.
 */
class ShaderStageD3D12 : public HexEngine::IShaderStage
{
public:
	virtual ~ShaderStageD3D12() override = default;

	virtual void  Destroy() override { _bytecode.clear(); }
	virtual void* GetNativePtr() override { return _bytecode.empty() ? nullptr : _bytecode.data(); }

	virtual bool  GetBinaryCode(std::vector<uint8_t>& code) override
	{
		code = _bytecode;
		return !code.empty();
	}

	virtual void  CopyFrom(IShaderStage* other) override
	{
		if (other == nullptr) return;
		other->GetBinaryCode(_bytecode);
	}

public:
	std::vector<uint8_t> _bytecode;
};
