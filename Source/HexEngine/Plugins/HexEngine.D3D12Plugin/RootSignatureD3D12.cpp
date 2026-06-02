
#include "RootSignatureD3D12.hpp"
#include <HexEngine.Core/HexEngine.hpp>

bool RootSignatureD3D12::Create(ID3D12Device* device)
{
	if (device == nullptr) return false;

	D3D12_DESCRIPTOR_RANGE ranges[3] = {};

	ranges[0].RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
	ranges[0].NumDescriptors                    = kCbvCount;
	ranges[0].BaseShaderRegister                = 0;
	ranges[0].RegisterSpace                     = 0;
	ranges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	ranges[1].RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	ranges[1].NumDescriptors                    = kSrvCount;
	ranges[1].BaseShaderRegister                = 0;
	ranges[1].RegisterSpace                     = 0;
	ranges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	ranges[2].RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
	ranges[2].NumDescriptors                    = kUavCount;
	ranges[2].BaseShaderRegister                = 0;
	ranges[2].RegisterSpace                     = 0;
	ranges[2].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	D3D12_ROOT_PARAMETER params[3] = {};

	// Param 0: CBV table
	params[0].ParameterType                      = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	params[0].DescriptorTable.NumDescriptorRanges = 1;
	params[0].DescriptorTable.pDescriptorRanges  = &ranges[0];
	params[0].ShaderVisibility                   = D3D12_SHADER_VISIBILITY_ALL;

	// Param 1: SRV table
	params[1].ParameterType                      = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	params[1].DescriptorTable.NumDescriptorRanges = 1;
	params[1].DescriptorTable.pDescriptorRanges  = &ranges[1];
	params[1].ShaderVisibility                   = D3D12_SHADER_VISIBILITY_ALL;

	// Param 2: UAV table
	params[2].ParameterType                      = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	params[2].DescriptorTable.NumDescriptorRanges = 1;
	params[2].DescriptorTable.pDescriptorRanges  = &ranges[2];
	params[2].ShaderVisibility                   = D3D12_SHADER_VISIBILITY_ALL;

	// Static samplers - match the D3D11 plugin's BeginFrame sampler array.
	D3D12_STATIC_SAMPLER_DESC samplers[5] = {};
	auto fillSampler = [](D3D12_STATIC_SAMPLER_DESC& s, UINT reg, D3D12_FILTER filter, D3D12_TEXTURE_ADDRESS_MODE addr)
	{
		s.Filter         = filter;
		s.AddressU       = addr;
		s.AddressV       = addr;
		s.AddressW       = addr;
		s.MipLODBias     = 0;
		s.MaxAnisotropy  = 16;
		s.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
		s.BorderColor    = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
		s.MinLOD         = 0.0f;
		s.MaxLOD         = D3D12_FLOAT32_MAX;
		s.ShaderRegister = reg;
		s.RegisterSpace  = 0;
		s.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	};
	fillSampler(samplers[0], 0, D3D12_FILTER_ANISOTROPIC,                          D3D12_TEXTURE_ADDRESS_MODE_WRAP);
	fillSampler(samplers[1], 1, D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT,  D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
	samplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	fillSampler(samplers[2], 2, D3D12_FILTER_MIN_MAG_MIP_POINT,                    D3D12_TEXTURE_ADDRESS_MODE_WRAP);
	fillSampler(samplers[3], 3, D3D12_FILTER_MIN_MAG_MIP_LINEAR,                   D3D12_TEXTURE_ADDRESS_MODE_MIRROR);
	fillSampler(samplers[4], 4, D3D12_FILTER_MIN_MAG_MIP_LINEAR,                   D3D12_TEXTURE_ADDRESS_MODE_WRAP);

	D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
	rsDesc.NumParameters     = (UINT)_countof(params);
	rsDesc.pParameters       = params;
	rsDesc.NumStaticSamplers = (UINT)_countof(samplers);
	rsDesc.pStaticSamplers   = samplers;
	rsDesc.Flags             = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	Microsoft::WRL::ComPtr<ID3DBlob> serialised;
	Microsoft::WRL::ComPtr<ID3DBlob> errors;
	HRESULT hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1_0, &serialised, &errors);
	if (FAILED(hr))
	{
		LOG_CRIT("D3D12SerializeRootSignature failed (0x%X)%s%s", hr,
			errors ? ": " : "",
			errors ? (const char*)errors->GetBufferPointer() : "");
		return false;
	}

	hr = device->CreateRootSignature(0, serialised->GetBufferPointer(), serialised->GetBufferSize(), IID_PPV_ARGS(&_rootSig));
	if (FAILED(hr))
	{
		LOG_CRIT("CreateRootSignature failed (0x%X)", hr);
		return false;
	}
	return true;
}
