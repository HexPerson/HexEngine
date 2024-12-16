
#pragma once

#include <HexEngine.Core/HexEngine.hpp>

class MyInterface : public IPluginInterface
{
public:
	virtual bool Create() override;

	virtual void Destroy() override;
};
