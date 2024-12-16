

#pragma once

#include <HexEngine.Core\HexEngine.hpp>

class ToolBase
{
public:
	virtual void Create() = 0;

	virtual void Update(int x, int y) = 0;
};
