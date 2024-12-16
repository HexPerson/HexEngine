
#pragma once

#include <HexEngine.Core/HexEngine.hpp>

class MyInterface : public IPluginInterface
{
public:
	virtual void Destroy() override;
};
