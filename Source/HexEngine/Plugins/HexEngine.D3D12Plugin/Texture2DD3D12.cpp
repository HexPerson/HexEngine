
#include "Texture2DD3D12.hpp"
#include "GraphicsDeviceD3D12.hpp"
#include <HexEngine.Core/HexEngine.hpp>

// The clear is a command-list operation, not a resource-level one. Find the
// active D3D12 device via the global env and reuse its open command list +
// barrier tracker.
void Texture2DD3D12::ClearRenderTargetView(const math::Color& colour)
{
	auto* device = static_cast<GraphicsDeviceD3D12*>(HexEngine::g_pEnv->_graphicsDevice);
	if (device == nullptr)
		return;

	auto* cmd = device->GetActiveCommandList();
	if (cmd == nullptr || _resource == nullptr)
		return;

	device->TransitionResource(this, D3D12_RESOURCE_STATE_RENDER_TARGET);

	const float clear[4] = { colour.R(), colour.G(), colour.B(), colour.A() };
	cmd->ClearRenderTargetView(_rtv, clear, 0, nullptr);
}
