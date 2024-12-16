

#pragma once

#include "BaseCompiler.hpp"
#include <d3dcompiler.h>

class HLSL : 
	public BaseCompiler,
	public ID3DInclude
{
public:
	virtual bool Compile(const fs::path& filePath, std::vector<uint8_t>& dataOut, HexEngine::ShaderFileFormat& shader) override;

	// virtual overrides from ID3DInclude
	virtual HRESULT Open(D3D_INCLUDE_TYPE IncludeType, LPCSTR pFileName, LPCVOID pParentData, LPCVOID* ppData, UINT* pBytes) override;
	virtual HRESULT Close(LPCVOID pData) override;
};