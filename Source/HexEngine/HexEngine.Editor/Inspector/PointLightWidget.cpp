

#include "PointLightWidget.h"

#include <qlineedit.h>
#include <qcolordialog.h>

PointLightWidget::PointLightWidget(HexEngine::Entity* entity, QWidget* parent) :
	BaseWidget(parent),
	_entity(entity)
{
	ui.setupUi(this);

	_pointLight = entity->GetComponent<HexEngine::PointLight>();

	ui.pointLightRadius->setValue(_pointLight->GetRadius());
	ui.pointLightIntensity->setValue((int)(_pointLight->GetDiffuseColour().w * 255.0f));

	connect(ui.pointLightRadius, &QSlider::valueChanged, this, &PointLightWidget::RadiusChanged);
	connect(ui.pointLightIntensity, &QSlider::valueChanged, this, &PointLightWidget::IntensityChanged);

	ui.pointLightColour->installEventFilter(this);

	auto diffuseColour = _pointLight->GetDiffuseColour();

	QColor currentColour(diffuseColour.x * 255.0f, diffuseColour.y * 255.0f, diffuseColour.z * 255.0f);

	QString style("background-color: " + currentColour.name() + ";");
	ui.pointLightColour->setStyleSheet(style);

	ui.effect->addItem("None");
	ui.effect->addItem("Slow Random Pulse");

	connect(ui.effect, &QComboBox::currentIndexChanged, this, &PointLightWidget::SetEffect);

	resize(0, 0);
}

void PointLightWidget::RadiusChanged(int value)
{
	_pointLight->SetRadius((float)value);
}

void PointLightWidget::IntensityChanged(int value)
{
	auto diffuse = _pointLight->GetDiffuseColour();

	diffuse.w = (float)value / 255.0f;

	_pointLight->SetDiffuseColour(diffuse);
}

void PointLightWidget::ChangeColour()
{
	bool a = false;
}

bool PointLightWidget::eventFilter(QObject* object, QEvent* event)
{
	if (object == ui.pointLightColour && event->type() == QEvent::MouseButtonPress) {

		QColorDialog* picker = new QColorDialog(this);

		//picker->setWindowFlags(Qt::Widget);

		//picker->setOptions(
		//	/* do not use native dialog */
		//	QColorDialog::DontUseNativeDialog
		//	/* you don't need to set it, but if you don't set this
		//		the "OK" and "Cancel" buttons will show up, I don't
		//		think you'd want that. */
		//	| QColorDialog::NoButtons
		//);

		//picker->show();

		picker->exec();

		auto colour = picker->selectedColor();

		QString style("background-color: " + colour.name() + ";");

		ui.pointLightColour->setStyleSheet(style);

		auto diffuse = _pointLight->GetDiffuseColour();

		_pointLight->SetDiffuseColour(math::Color(colour.redF(), colour.greenF(), colour.blueF(), diffuse.w));

		return true; // lets the event continue to the edit
	}
	return false;
}

void PointLightWidget::SetEffect(int effect)
{
	_pointLight->SetLightingEffect((HexEngine::LightingEffect)effect);
}