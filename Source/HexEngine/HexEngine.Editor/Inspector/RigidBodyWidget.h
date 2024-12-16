
#pragma once

#include "BaseWidget.h"

#include "ui_RigidBodyWidget.h"

#include <HexEngine.Core\Entity\Entity.hpp>
#include <HexEngine.Core\Entity\Component\RigidBody.hpp>

class RigidBodyWidget : public BaseWidget
{
public:
	RigidBodyWidget(HexEngine::Entity* entity, QWidget* parent);

private:
	void OnChangeCollider(int index);

private:
	Ui::RigidBodyWidget ui;
	HexEngine::Entity* _entity = nullptr;
	HexEngine::RigidBody* _rigidBody = nullptr;
};
