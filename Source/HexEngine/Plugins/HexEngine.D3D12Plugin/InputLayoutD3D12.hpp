
#pragma once

#include <HexEngine.Core/Graphics/IInputLayout.hpp>
#include <d3d12.h>
#include <vector>
#include <string>

/**
 * @brief D3D12 input layout.
 *
 * Unlike D3D11 which has standalone ID3D11InputLayout objects, D3D12's input
 * layout is part of the PSO. We just store the descriptor array here and
 * the PSO cache (Phase B4) reads it when assembling the pipeline state.
 *
 * The semantic-name std::strings keep the LPCSTR pointers in `_elements`
 * valid for the lifetime of this object.
 */
class InputLayoutD3D12 : public HexEngine::IInputLayout
{
public:
	virtual ~InputLayoutD3D12() override = default;

	virtual void  Destroy() override
	{
		_elements.clear();
		_semanticNames.clear();
	}
	virtual void* GetNativePtr() override { return _elements.data(); }

public:
	std::vector<std::string>                _semanticNames; ///< backs LPCSTRs in `_elements`
	std::vector<D3D12_INPUT_ELEMENT_DESC>   _elements;
};
