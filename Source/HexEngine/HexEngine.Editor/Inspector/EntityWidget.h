
#pragma once

#include "BaseWidget.h"

#include "ui_EntityWidget.h"

#include <HexEngine.Core\Entity\Entity.hpp>

class EntityWidget : public BaseWidget
{
public:
	EntityWidget(HexEngine::Entity* entity, QWidget* parent);

private:
	void NameChanged(const QString& value);
	void AddComponent();
	void OnAddComponent(const QModelIndex& index);
	
private:
	Ui::EntityWidget ui;
	HexEngine::Entity* _entity = nullptr;
};
