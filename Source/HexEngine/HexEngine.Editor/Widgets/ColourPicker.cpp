

#include "ColourPicker.hpp"
#include <qcolordialog.h>

ColourPicker::ColourPicker(QWidget* parent, const QColor& currentColour) :
	QLineEdit(parent),
	_currentColour(currentColour)
{
	// set the existing colour
	QString style("background-color: " + currentColour.name() + ";");
	setStyleSheet(style);

	installEventFilter(this);
}

bool ColourPicker::eventFilter(QObject* object, QEvent* event)
{
	if (object == this && event->type() == QEvent::MouseButtonPress) {

		QColorDialog* picker = new QColorDialog();

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

		this->setStyleSheet(style);

		_currentColour = colour;

		emit ColourChanged(colour);

		return true; // lets the event continue to the edit
	}
	return false;
}