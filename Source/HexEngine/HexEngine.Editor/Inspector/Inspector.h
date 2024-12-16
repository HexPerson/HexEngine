
#pragma once

#define DISABLE_MEM_TRACKING 1
#include <HexEngine.Core\Required.hpp>
#include <HexEngine.Core\Entity\Entity.hpp>
#define DISABLE_MEM_TRACKING 0

#include <qwidget.h>

class Inspector
{
public:
	Inspector();

	void InspectEntity(HexEngine::Entity* entity);

	void InspectMaterial(HexEngine::Material* material);

	void AppendWidget(QWidget* widget);
};

inline Inspector gInspector;
