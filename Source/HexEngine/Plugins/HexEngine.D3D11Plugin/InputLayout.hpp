

#pragma once

#include <HexEngine.Core/Graphics/IInputLayout.hpp>

namespace HexEngine
{
	class InputLayout : public IInputLayout
	{
		friend class GraphicsDeviceD3D11;

	public:
		virtual ~InputLayout();

		virtual void Destroy() override;

		virtual void* GetNativePtr() override;

		virtual void SetDebugName(const std::string& name) override
		{
#ifdef _DEBUG
			((ID3D11InputLayout*)GetNativePtr())->SetPrivateData(WKPDID_D3DDebugObjectName, (UINT)name.length(), name.data());
#endif
		}
		
	private:
		ID3D11InputLayout* _inputLayout = nullptr;
	};
}
