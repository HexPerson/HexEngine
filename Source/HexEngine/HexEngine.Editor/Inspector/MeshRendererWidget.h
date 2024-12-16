
#pragma once

#include "BaseWidget.h"

#include "ui_MeshRendererWidget.h"

#include <HexEngine.Core\Entity\Entity.hpp>

class MeshRendererWidget : public BaseWidget
{
public:
	MeshRendererWidget(HexEngine::Entity* entity, QWidget* parent);

private:
	void SelectMesh(int index);
	void SelectSplatMap();

private:
	Ui::MeshRendererWidget ui;
	HexEngine::Entity* _entity = nullptr;
	HexEngine::MeshRenderer* _meshRenderer = nullptr;
};
