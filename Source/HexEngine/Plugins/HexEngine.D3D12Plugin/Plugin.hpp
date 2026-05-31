
#pragma once

#include <HexEngine.Core/HexEngine.hpp>
#include "GraphicsDeviceD3D12.hpp"

/**
 * @brief D3D12 renderer plugin.
 *
 * Currently a non-functional skeleton: GraphicsDeviceD3D12 implements
 * IGraphicsDevice with stub methods that LOG_CRIT / assert / return null.
 * The skeleton exists so:
 *   1. The renderer-selection cvar (r_renderer) has a real second target to
 *      pick - the engine can route to D3D12 instead of D3D11 by setting
 *      r_renderer = 2, even though startup will fail loudly.
 *   2. Phase B can fill in methods one at a time without scaffolding work.
 *   3. The IGraphicsDevice surface is exercised by a non-D3D11 implementer
 *      from day one, so the abstraction's leak-tightness is testable.
 *
 * When r_renderer is auto (0) or d3d11 (1), this plugin's CreateInterface
 * returns null and the engine picks the D3D11 plugin instead.
 */
class D3D12Plugin : public HexEngine::IPlugin
{
public:
	D3D12Plugin();

	virtual void Destroy() override;

	virtual void GetVersionData(VersionData* data) override;

	virtual HexEngine::IPluginInterface* CreateInterface(const std::string& interfaceName) override;

	virtual void GetDependencies(std::vector<std::string>& dependencies) const override {}

private:
	GraphicsDeviceD3D12* _device = nullptr;
};

inline D3D12Plugin* g_pD3D12Plugin = nullptr;
