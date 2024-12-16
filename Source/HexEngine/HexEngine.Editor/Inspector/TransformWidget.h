
#pragma once

#include "BaseWidget.h"

#include "ui_TransformWidget.h"

#include <HexEngine.Core\Entity\Entity.hpp>

class TransformWidget : public BaseWidget
{
public:
	TransformWidget(HexEngine::Entity* entity, QWidget* parent);

private:
	void PosXChanged(double val);
	void PosYChanged(double val);
	void PosZChanged(double val);

	void RotXChanged(double val);
	void RotYChanged(double val);
	void RotZChanged(double val);

	void ScaleXChanged(double val);
	void ScaleYChanged(double val);
	void ScaleZChanged(double val);

private:
	Ui::TransformWidget ui;
	HexEngine::Entity* _entity = nullptr;
};
