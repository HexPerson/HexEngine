
#pragma once

#include <qwidget.h>
#include <qlineedit.h>
#include <qevent.h>

class ColourPicker : public QLineEdit
{
	Q_OBJECT

public:
	ColourPicker(QWidget* parent, const QColor& currentColour);

signals:
	void ColourChanged(QColor newColour);

private:
	bool eventFilter(QObject* object, QEvent* event);

private:
	QColor _currentColour;
};
