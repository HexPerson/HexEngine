
#pragma once

#include "BaseWidget.h"

#include "ui_PointLightWidget.h"

#include <HexEngine.Core\Entity\Entity.hpp>
#include <HexEngine.Core\Entity\Component\PointLight.hpp>

class PointLightWidget : public BaseWidget
{
public:
	PointLightWidget(HexEngine::Entity* entity, QWidget* parent);

private:
	void RadiusChanged(int value);
	void IntensityChanged(int value);
	void ChangeColour();
	bool eventFilter(QObject* object, QEvent* event);
	void SetEffect(int effect);

private:
	Ui::PointLightWidget ui;
	HexEngine::Entity* _entity = nullptr;
	HexEngine::PointLight* _pointLight = nullptr;
};
