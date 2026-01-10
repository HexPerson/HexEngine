
#pragma once

#include "INativeGraphicsResource.hpp"
#include "IShaderStage.hpp"

namespace HexEngine
{
	enum class InputLayoutId : uint8_t
	{
		Pos,
		Pos_INSTANCED,

		PosTex,
		PosTex_INSTANCED,
		PosTex_INSTANCED_SIMPLE,

		PosNormTanBinTex_INSTANCED,

		PosNormTanBinTexBoned_INSTANCED,
		PosTexBoned_INSTANCED_SIMPLE,

		PosColour,
		PosTexColour,
		UI_INSTANCED,

		Count
	};

	class IInputLayout : public INativeGraphicsResource
	{
	public:
		virtual ~IInputLayout() {}		

		static void Destroy();

		static IInputLayout* GetInputLayoutById(InputLayoutId id, IShaderStage* vertexStage);

		static IInputLayout* GetLayout_PosNormTanBinTex_INSTANCED(IShaderStage* vertexShaderStage);
		static IInputLayout* GetLayout_PosNormTanBinTexBoned_INSTANCED(IShaderStage* vertexShaderStage);
		static IInputLayout* GetLayout_PosTexBoned_INSTANCED_SIMPLE(IShaderStage* vertexShaderStage);

		static IInputLayout* GetLayout_Pos(IShaderStage* vertexShaderStage);
		static IInputLayout* GetLayout_Pos_INSTANCED(IShaderStage* vertexShaderStage);

		static IInputLayout* GetLayout_PosTex_INSTANCED(IShaderStage* vertexShaderStage);
		static IInputLayout* GetLayout_PosTex_INSTANCED_SIMPLE(IShaderStage* vertexShaderStage);
		static IInputLayout* GetLayout_PosTex(IShaderStage* vertexShaderStage);

		static IInputLayout* GetLayout_PosColour(IShaderStage* vertexShaderStage);
		static IInputLayout* GetLayout_PosTexColour(IShaderStage* vertexShaderStage);
		static IInputLayout* GetLayout_UI_INSTANCED(IShaderStage* vertexShaderStage);
	};

}
