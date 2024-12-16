

#include "TransformWidget.h"

TransformWidget::TransformWidget(HexEngine::Entity* entity, QWidget* parent) :
	BaseWidget(parent),
	_entity(entity)
{
	ui.setupUi(this);

	ui.posX->setValue(entity->GetPosition().x);
	ui.posY->setValue(entity->GetPosition().y);
	ui.posZ->setValue(entity->GetPosition().z);

	connect(ui.posX, &QDoubleSpinBox::valueChanged, this, &TransformWidget::PosXChanged);
	connect(ui.posY, &QDoubleSpinBox::valueChanged, this, &TransformWidget::PosYChanged);
	connect(ui.posZ, &QDoubleSpinBox::valueChanged, this, &TransformWidget::PosZChanged);

	auto euler = entity->GetComponent<HexEngine::Transform>()->ToEulerAngles();

	ui.rotX->setValue(euler.x);
	ui.rotY->setValue(euler.y);
	ui.rotZ->setValue(euler.z);

	connect(ui.rotX, &QDoubleSpinBox::valueChanged, this, &TransformWidget::RotXChanged);
	connect(ui.rotY, &QDoubleSpinBox::valueChanged, this, &TransformWidget::RotYChanged);
	connect(ui.rotZ, &QDoubleSpinBox::valueChanged, this, &TransformWidget::RotZChanged);

	auto scale = entity->GetComponent<HexEngine::Transform>()->GetScale();

	ui.scaleX->setValue(scale.x);
	ui.scaleY->setValue(scale.y);
	ui.scaleZ->setValue(scale.z);

	connect(ui.scaleX, &QDoubleSpinBox::valueChanged, this, &TransformWidget::ScaleXChanged);
	connect(ui.scaleY, &QDoubleSpinBox::valueChanged, this, &TransformWidget::ScaleYChanged);
	connect(ui.scaleZ, &QDoubleSpinBox::valueChanged, this, &TransformWidget::ScaleZChanged);

	resize(0, 0);
}

void TransformWidget::PosXChanged(double val)
{
	auto currentPos = _entity->GetPosition();

	currentPos.x = (float)val;

	_entity->SetPosition(currentPos);
}

void TransformWidget::PosYChanged(double val)
{
	auto currentPos = _entity->GetPosition();

	currentPos.y = (float)val;

	_entity->SetPosition(currentPos);
}

void TransformWidget::PosZChanged(double val)
{
	auto currentPos = _entity->GetPosition();

	currentPos.z = (float)val;

	_entity->SetPosition(currentPos);
}

void TransformWidget::RotXChanged(double val)
{
	auto transform = _entity->GetComponent<HexEngine::Transform>();

	transform->SetYaw(ToRadian((float)val));
}

void TransformWidget::RotYChanged(double val)
{
	auto transform = _entity->GetComponent<HexEngine::Transform>();

	transform->SetPitch(ToRadian((float)val));
}

void TransformWidget::RotZChanged(double val)
{
	auto transform = _entity->GetComponent<HexEngine::Transform>();

	transform->SetRoll(ToRadian((float)val));
}

void TransformWidget::ScaleXChanged(double val)
{
	auto currentScale = _entity->GetScale();

	currentScale.x = (float)val;

	_entity->SetScale(currentScale);
}

void TransformWidget::ScaleYChanged(double val)
{
	auto currentScale = _entity->GetScale();

	currentScale.y = (float)val;

	_entity->SetScale(currentScale);
}

void TransformWidget::ScaleZChanged(double val)
{
	auto currentScale = _entity->GetScale();

	currentScale.z = (float)val;

	_entity->SetScale(currentScale);
}