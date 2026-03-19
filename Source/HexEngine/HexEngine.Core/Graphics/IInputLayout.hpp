
#pragma once

#include "INativeGraphicsResource.hpp"
#include "IShaderStage.hpp"

namespace HexEngine
{
	/** @brief Canonical engine vertex input layouts. */
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

	/** @brief Input-layout abstraction used by shader + vertex buffer binding. */
	class IInputLayout : public INativeGraphicsResource
	{
	public:
		virtual ~IInputLayout() {}		

		/** @brief Destroys cached static layout instances. */
		static void Destroy();

		/** @brief Returns an input layout by enum id using the supplied vertex stage bytecode. */
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
