
#pragma once

#include <HexEngine.Core/HexEngine.hpp>

class MyInterface : public HexEngine::IPluginInterface
{
public:
	virtual bool Create() override;

	virtual void Destroy() override;
};
