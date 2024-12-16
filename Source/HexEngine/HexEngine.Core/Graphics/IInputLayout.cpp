
#include "IInputLayout.hpp"
#include "../HexEngine.hpp"

namespace HexEngine
{
	static IInputLayout* g_pLayoutPosNormTanBinTex_INSTANCED = nullptr;
	static IInputLayout* g_pLayoutPosNormTanBinTexBoned_INSTANCED = nullptr;
	static IInputLayout* g_pLayoutPos = nullptr;
	static IInputLayout* g_pLayoutPos_INSTANCED = nullptr;
	static IInputLayout* g_pLayout_PosTex_INSTANCED = nullptr;
	static IInputLayout* g_pLayout_PosTex = nullptr;
	static IInputLayout* g_pLayout_PosColour = nullptr;
	static IInputLayout* g_pLayout_PosTexColour = nullptr;
	static IInputLayout* g_pLayout_UI_INSTANCED = nullptr;

	void IInputLayout::Destroy()
	{
		SAFE_DELETE(g_pLayoutPosNormTanBinTex_INSTANCED);
		SAFE_DELETE(g_pLayoutPos);
		SAFE_DELETE(g_pLayoutPos_INSTANCED);
		SAFE_DELETE(g_pLayout_PosTex_INSTANCED);
		SAFE_DELETE(g_pLayout_PosTex);
		SAFE_DELETE(g_pLayoutPosNormTanBinTexBoned_INSTANCED);
		SAFE_DELETE(g_pLayout_PosTexColour);
		SAFE_DELETE(g_pLayout_UI_INSTANCED);
	}

	IInputLayout* IInputLayout::GetInputLayoutById(InputLayoutId id, IShaderStage* vertexStage)
	{
		switch (id)
		{
		case InputLayoutId::Pos:
			return GetLayout_Pos(vertexStage);

		case InputLayoutId::Pos_INSTANCED:
			return GetLayout_Pos_INSTANCED(vertexStage);

		case InputLayoutId::PosTex:
			return GetLayout_PosTex(vertexStage);

		case InputLayoutId::PosTex_INSTANCED:
			return GetLayout_PosTex_INSTANCED(vertexStage);

		case InputLayoutId::PosNormTanBinTex_INSTANCED:
			return GetLayout_PosNormTanBinTex_INSTANCED(vertexStage);

		case InputLayoutId::PosNormTanBinTexBoned_INSTANCED:
			return GetLayout_PosNormTanBinTexBoned_INSTANCED(vertexStage);

		case InputLayoutId::PosColour:
			return GetLayout_PosColour(vertexStage);

		case InputLayoutId::PosTexColour:
			return GetLayout_PosTexColour(vertexStage);

		case InputLayoutId::UI_INSTANCED:
			return GetLayout_UI_INSTANCED(vertexStage);

		default:
			LOG_CRIT("Unhandled input layout id: %d", id);
			return nullptr;
		}
	}

	IInputLayout* IInputLayout::GetLayout_PosNormTanBinTex_INSTANCED(IShaderStage* vertexShaderStage)
	{
		if (!g_pLayoutPosNormTanBinTex_INSTANCED)
		{
			D3D11_INPUT_ELEMENT_DESC inputDesc[] = {
				{"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0},
				{"NORMAL", 0,	DXGI_FORMAT_R32G32B32_FLOAT,	0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
				{"TANGENT", 0,	DXGI_FORMAT_R32G32B32_FLOAT,	0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0},
				{"BINORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT,	0, 40, D3D11_INPUT_PER_VERTEX_DATA, 0},
				{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,		0, 52, D3D11_INPUT_PER_VERTEX_DATA, 0},

				// Data from the instance buffer
				{ "WORLD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT,    1, 0,  D3D11_INPUT_PER_INSTANCE_DATA, 1},
				{ "WORLD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT,    1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1},
				{ "WORLD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT,    1, 32, D3D11_INPUT_PER_INSTANCE_DATA, 1},
				{ "WORLD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT,    1, 48, D3D11_INPUT_PER_INSTANCE_DATA, 1},

				{ "WORLDIT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT,    1, 64,  D3D11_INPUT_PER_INSTANCE_DATA, 1},
				{ "WORLDIT", 1, DXGI_FORMAT_R32G32B32A32_FLOAT,    1, 80, D3D11_INPUT_PER_INSTANCE_DATA, 1},
				{ "WORLDIT", 2, DXGI_FORMAT_R32G32B32A32_FLOAT,    1, 96, D3D11_INPUT_PER_INSTANCE_DATA, 1},
				{ "WORLDIT", 3, DXGI_FORMAT_R32G32B32A32_FLOAT,    1, 112, D3D11_INPUT_PER_INSTANCE_DATA, 1},

				{ "WORLDPREV", 0, DXGI_FORMAT_R32G32B32A32_FLOAT,    1, 128,  D3D11_INPUT_PER_INSTANCE_DATA, 1},
				{ "WORLDPREV", 1, DXGI_FORMAT_R32G32B32A32_FLOAT,    1, 144, D3D11_INPUT_PER_INSTANCE_DATA, 1},
				{ "WORLDPREV", 2, DXGI_FORMAT_R32G32B32A32_FLOAT,    1, 160, D3D11_INPUT_PER_INSTANCE_DATA, 1},
				{ "WORLDPREV", 3, DXGI_FORMAT_R32G32B32A32_FLOAT,    1, 176, D3D11_INPUT_PER_INSTANCE_DATA, 1},

				{ "INSTANCECOLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT,    1, 192, D3D11_INPUT_PER_INSTANCE_DATA, 1},
				{ "UVSCALE", 0, DXGI_FORMAT_R32G32_FLOAT,    1, 208, D3D11_INPUT_PER_INSTANCE_DATA, 1},

				//{ "INSTANCEID", 0, DXGI_FORMAT_R32_UINT,    1, 224, D3D11_INPUT_PER_INSTANCE_DATA, 1 }
			};

			
			std::vector<uint8_t> vertexShaderCode;
			if (vertexShaderStage->GetBinaryCode(vertexShaderCode))
			{
				g_pLayoutPosNormTanBinTex_INSTANCED = g_pEnv->_graphicsDevice->CreateInputLayout(inputDesc, _countof(inputDesc), vertexShaderCode);
			}
		}

		return g_pLayoutPosNormTanBinTex_INSTANCED;
	}

	IInputLayout* IInputLayout::GetLayout_PosNormTanBinTexBoned_INSTANCED(IShaderStage* vertexShaderStage)
	{
		if (!g_pLayoutPosNormTanBinTexBoned_INSTANCED)
		{
			D3D11_INPUT_ELEMENT_DESC inputDesc[] = {
				{"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0},
				{"NORMAL", 0,	DXGI_FORMAT_R32G32B32_FLOAT,	0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
				{"TANGENT", 0,	DXGI_FORMAT_R32G32B32_FLOAT,	0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0},
				{"BINORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT,	0, 40, D3D11_INPUT_PER_VERTEX_DATA, 0},
				{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,		0, 52, D3D11_INPUT_PER_VERTEX_DATA, 0},
				{"BLENDINDICES", 0,	DXGI_FORMAT_R32G32B32A32_FLOAT,	0, 60, D3D11_INPUT_PER_VERTEX_DATA, 0},
				{"BLENDWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT,	0, 76, D3D11_INPUT_PER_VERTEX_DATA, 0},

				// Data from the instance buffer
				{ "WORLD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT,    1, 0,  D3D11_INPUT_PER_INSTANCE_DATA, 1},
				{ "WORLD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT,    1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1},
				{ "WORLD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT,    1, 32, D3D11_INPUT_PER_INSTANCE_DATA, 1},
				{ "WORLD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT,    1, 48, D3D11_INPUT_PER_INSTANCE_DATA, 1},

				{ "WORLDIT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT,    1, 64,  D3D11_INPUT_PER_INSTANCE_DATA, 1},
				{ "WORLDIT", 1, DXGI_FORMAT_R32G32B32A32_FLOAT,    1, 80, D3D11_INPUT_PER_INSTANCE_DATA, 1},
				{ "WORLDIT", 2, DXGI_FORMAT_R32G32B32A32_FLOAT,    1, 96, D3D11_INPUT_PER_INSTANCE_DATA, 1},
				{ "WORLDIT", 3, DXGI_FORMAT_R32G32B32A32_FLOAT,    1, 112, D3D11_INPUT_PER_INSTANCE_DATA, 1},

				{ "WORLDPREV", 0, DXGI_FORMAT_R32G32B32A32_FLOAT,    1, 128,  D3D11_INPUT_PER_INSTANCE_DATA, 1},
				{ "WORLDPREV", 1, DXGI_FORMAT_R32G32B32A32_FLOAT,    1, 144, D3D11_INPUT_PER_INSTANCE_DATA, 1},
				{ "WORLDPREV", 2, DXGI_FORMAT_R32G32B32A32_FLOAT,    1, 160, D3D11_INPUT_PER_INSTANCE_DATA, 1},
				{ "WORLDPREV", 3, DXGI_FORMAT_R32G32B32A32_FLOAT,    1, 176, D3D11_INPUT_PER_INSTANCE_DATA, 1},

				{ "INSTANCECOLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT,    1, 192, D3D11_INPUT_PER_INSTANCE_DATA, 1},
				{ "UVSCALE", 0, DXGI_FORMAT_R32G32_FLOAT,    1, 208, D3D11_INPUT_PER_INSTANCE_DATA, 1}
			};


			std::vector<uint8_t> vertexShaderCode;
			if (vertexShaderStage->GetBinaryCode(vertexShaderCode))
			{
				g_pLayoutPosNormTanBinTexBoned_INSTANCED = g_pEnv->_graphicsDevice->CreateInputLayout(inputDesc, _countof(inputDesc), vertexShaderCode);

				g_pLayoutPosNormTanBinTexBoned_INSTANCED->SetDebugName("PosNormTanBinTexBoned_INSTANCED");
			}
		}

		return g_pLayoutPosNormTanBinTexBoned_INSTANCED;
	}

	IInputLayout* IInputLayout::GetLayout_Pos(IShaderStage* vertexShaderStage)
	{
		if (!g_pLayoutPos)
		{
			D3D11_INPUT_ELEMENT_DESC inputDesc[] = {
				{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
			};

			std::vector<uint8_t> vertexShaderCode;
			if (vertexShaderStage->GetBinaryCode(vertexShaderCode))
			{
				g_pLayoutPos = g_pEnv->_graphicsDevice->CreateInputLayout(inputDesc, _countof(inputDesc), vertexShaderCode);
			}
		}

		return g_pLayoutPos;
	}

	IInputLayout* IInputLayout::GetLayout_Pos_INSTANCED(IShaderStage* vertexShaderStage)
	{
		if (!g_pLayoutPos_INSTANCED)
		{
			D3D11_INPUT_ELEMENT_DESC inputDesc[] = {
				{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,	 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},

				// Data from the instance buffer
				{ "WORLD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT,    1, 0,  D3D11_INPUT_PER_INSTANCE_DATA, 1},
				{ "WORLD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT,    1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1},
				{ "WORLD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT,    1, 32, D3D11_INPUT_PER_INSTANCE_DATA, 1},
				{ "WORLD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT,    1, 48, D3D11_INPUT_PER_INSTANCE_DATA, 1},

				{ "WORLDIT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT,    1, 64,  D3D11_INPUT_PER_INSTANCE_DATA, 1},
				{ "WORLDIT", 1, DXGI_FORMAT_R32G32B32A32_FLOAT,    1, 80, D3D11_INPUT_PER_INSTANCE_DATA, 1},
				{ "WORLDIT", 2, DXGI_FORMAT_R32G32B32A32_FLOAT,    1, 96, D3D11_INPUT_PER_INSTANCE_DATA, 1},
				{ "WORLDIT", 3, DXGI_FORMAT_R32G32B32A32_FLOAT,    1, 112, D3D11_INPUT_PER_INSTANCE_DATA, 1},

				{ "WORLDPREV", 0, DXGI_FORMAT_R32G32B32A32_FLOAT,    1, 128,  D3D11_INPUT_PER_INSTANCE_DATA, 1},
				{ "WORLDPREV", 1, DXGI_FORMAT_R32G32B32A32_FLOAT,    1, 144, D3D11_INPUT_PER_INSTANCE_DATA, 1},
				{ "WORLDPREV", 2, DXGI_FORMAT_R32G32B32A32_FLOAT,    1, 160, D3D11_INPUT_PER_INSTANCE_DATA, 1},
				{ "WORLDPREV", 3, DXGI_FORMAT_R32G32B32A32_FLOAT,    1, 176, D3D11_INPUT_PER_INSTANCE_DATA, 1},

				{ "INSTANCECOLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT,    1, 192, D3D11_INPUT_PER_INSTANCE_DATA, 1},
				{ "UVSCALE", 0, DXGI_FORMAT_R32G32_FLOAT,    1, 208, D3D11_INPUT_PER_INSTANCE_DATA, 1}
			};

			std::vector<uint8_t> vertexShaderCode;
			if (vertexShaderStage->GetBinaryCode(vertexShaderCode))
			{
				g_pLayoutPos_INSTANCED = g_pEnv->_graphicsDevice->CreateInputLayout(inputDesc, _countof(inputDesc), vertexShaderCode);
			}
		}

		return g_pLayoutPos_INSTANCED;
	}

	IInputLayout* IInputLayout::GetLayout_PosTex_INSTANCED(IShaderStage* vertexShaderStage)
	{
		if (!g_pLayout_PosTex_INSTANCED)
		{
			D3D11_INPUT_ELEMENT_DESC inputDesc[] = {
				{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,	0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
				{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,		0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},

				// Data from the instance buffer
				{ "WORLD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT,    1, 0,  D3D11_INPUT_PER_INSTANCE_DATA, 1},
				{ "WORLD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT,    1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1},
				{ "WORLD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT,    1, 32, D3D11_INPUT_PER_INSTANCE_DATA, 1},
				{ "WORLD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT,    1, 48, D3D11_INPUT_PER_INSTANCE_DATA, 1},

				{ "WORLDIT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT,    1, 64,  D3D11_INPUT_PER_INSTANCE_DATA, 1},
				{ "WORLDIT", 1, DXGI_FORMAT_R32G32B32A32_FLOAT,    1, 80, D3D11_INPUT_PER_INSTANCE_DATA, 1},
				{ "WORLDIT", 2, DXGI_FORMAT_R32G32B32A32_FLOAT,    1, 96, D3D11_INPUT_PER_INSTANCE_DATA, 1},
				{ "WORLDIT", 3, DXGI_FORMAT_R32G32B32A32_FLOAT,    1, 112, D3D11_INPUT_PER_INSTANCE_DATA, 1},

				{ "WORLDPREV", 0, DXGI_FORMAT_R32G32B32A32_FLOAT,    1, 128,  D3D11_INPUT_PER_INSTANCE_DATA, 1},
				{ "WORLDPREV", 1, DXGI_FORMAT_R32G32B32A32_FLOAT,    1, 144, D3D11_INPUT_PER_INSTANCE_DATA, 1},
				{ "WORLDPREV", 2, DXGI_FORMAT_R32G32B32A32_FLOAT,    1, 160, D3D11_INPUT_PER_INSTANCE_DATA, 1},
				{ "WORLDPREV", 3, DXGI_FORMAT_R32G32B32A32_FLOAT,    1, 176, D3D11_INPUT_PER_INSTANCE_DATA, 1},

				{ "INSTANCECOLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT,    1, 192, D3D11_INPUT_PER_INSTANCE_DATA, 1},
				{ "UVSCALE", 0, DXGI_FORMAT_R32G32_FLOAT,    1, 208, D3D11_INPUT_PER_INSTANCE_DATA, 1}

				
			};

			std::vector<uint8_t> vertexShaderCode;
			if (vertexShaderStage->GetBinaryCode(vertexShaderCode))
			{
				g_pLayout_PosTex_INSTANCED = g_pEnv->_graphicsDevice->CreateInputLayout(inputDesc, _countof(inputDesc), vertexShaderCode);
			}
		}

		return g_pLayout_PosTex_INSTANCED;
	}

	IInputLayout* IInputLayout::GetLayout_PosTex(IShaderStage* vertexShaderStage)
	{
		if (!g_pLayout_PosTex)
		{
			D3D11_INPUT_ELEMENT_DESC inputDesc[] = {
				{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,	0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
				{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,		0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0}
			};

			std::vector<uint8_t> vertexShaderCode;
			if (vertexShaderStage->GetBinaryCode(vertexShaderCode))
			{
				g_pLayout_PosTex = g_pEnv->_graphicsDevice->CreateInputLayout(inputDesc, _countof(inputDesc), vertexShaderCode);
			}
		}

		return g_pLayout_PosTex;
	}

	IInputLayout* IInputLayout::GetLayout_PosColour(IShaderStage* vertexShaderStage)
	{
		if (!g_pLayout_PosColour)
		{
			D3D11_INPUT_ELEMENT_DESC inputDesc[] = {
				{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,	0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
				{"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT,	0, 12 , D3D11_INPUT_PER_VERTEX_DATA, 0}
			};

			std::vector<uint8_t> vertexShaderCode;
			if (vertexShaderStage->GetBinaryCode(vertexShaderCode))
			{
				g_pLayout_PosColour = g_pEnv->_graphicsDevice->CreateInputLayout(inputDesc, _countof(inputDesc), vertexShaderCode);
			}
		}

		return g_pLayout_PosColour;
	}

	IInputLayout* IInputLayout::GetLayout_PosTexColour(IShaderStage* vertexShaderStage)
	{
		if (!g_pLayout_PosTexColour)
		{
			D3D11_INPUT_ELEMENT_DESC inputDesc[] = {
				{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,	0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
				{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,		0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
				{"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT,	0, 24 , D3D11_INPUT_PER_VERTEX_DATA, 0}
			};

			std::vector<uint8_t> vertexShaderCode;
			if (vertexShaderStage->GetBinaryCode(vertexShaderCode))
			{
				g_pLayout_PosTexColour = g_pEnv->_graphicsDevice->CreateInputLayout(inputDesc, _countof(inputDesc), vertexShaderCode);
			}
		}

		return g_pLayout_PosTexColour;
	}

	IInputLayout* IInputLayout::GetLayout_UI_INSTANCED(IShaderStage* vertexShaderStage)
	{
		if (!g_pLayout_UI_INSTANCED)
		{
			D3D11_INPUT_ELEMENT_DESC inputDesc[] = {
				{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,	0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
				//{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,		0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
				//{"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT,	0, 24 , D3D11_INPUT_PER_VERTEX_DATA, 0},

				{"CENTER", 0, DXGI_FORMAT_R32G32_FLOAT,			1, 0,	D3D11_INPUT_PER_INSTANCE_DATA, 1},
				{"SCALE", 0, DXGI_FORMAT_R32G32_FLOAT,			1, 8,	D3D11_INPUT_PER_INSTANCE_DATA, 1},
				{"TEXCOORDTL", 0, DXGI_FORMAT_R32G32_FLOAT,		1, 16,	D3D11_INPUT_PER_INSTANCE_DATA, 1},
				{"TEXCOORDBR", 0, DXGI_FORMAT_R32G32_FLOAT,		1, 24,	D3D11_INPUT_PER_INSTANCE_DATA, 1},
				{"COLORT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT,	1, 32 , D3D11_INPUT_PER_INSTANCE_DATA, 1},
				{"COLORB", 0, DXGI_FORMAT_R32G32B32A32_FLOAT,	1, 48 , D3D11_INPUT_PER_INSTANCE_DATA, 1},

				{ "ROTATION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT,    1, 64,  D3D11_INPUT_PER_INSTANCE_DATA, 1},
				{ "ROTATION", 1, DXGI_FORMAT_R32G32B32A32_FLOAT,    1, 80, D3D11_INPUT_PER_INSTANCE_DATA, 1},
				{ "ROTATION", 2, DXGI_FORMAT_R32G32B32A32_FLOAT,    1, 96, D3D11_INPUT_PER_INSTANCE_DATA, 1},
				{ "ROTATION", 3, DXGI_FORMAT_R32G32B32A32_FLOAT,    1, 112, D3D11_INPUT_PER_INSTANCE_DATA, 1},
			};

			std::vector<uint8_t> vertexShaderCode;
			if (vertexShaderStage->GetBinaryCode(vertexShaderCode))
			{
				g_pLayout_UI_INSTANCED = g_pEnv->_graphicsDevice->CreateInputLayout(inputDesc, _countof(inputDesc), vertexShaderCode);
			}
		}

		return g_pLayout_UI_INSTANCED;
	}
}