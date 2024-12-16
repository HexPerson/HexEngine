

#pragma once

#include <qwidget.h>
#include <qpainter.h>
#include <qpainterpath.h>

#include <HexEngine.Core\HexEngine.hpp>

class BaseWidget : public QWidget
{
public:
	BaseWidget(QWidget* parent = nullptr) :
		QWidget(parent)
	{
		setStyleSheet("background-color: rgb(83, 83, 83); font-family: 'Exo 2';");
	}

	void paintEvent(QPaintEvent* e) override
	{
		QPainter painter(this);		

		// background
		painter.fillRect(0, 0, width(), height(), QColor(83, 83, 83, 255));

		painter.setPen(QColor(10, 10, 10, 255));
		painter.drawRoundedRect(0, 0, width(), height(), 3, 3);

		// title bar
		painter.fillRect(1, 1, width()-2, 16, QColor(41, 41, 41, 255));
		painter.setPen(QColor(10, 10, 10, 255));
		painter.drawLine(0, 16, width(), 16);
		
		//painter.drawText(QPoint(8, 12), this->windowTitle());

		//QPainterPath path;

		QFont font;
		//font.setFamily("Exo 2");
		font.setPixelSize(10);
		font.setBold(false);
		//path.addText(8, 13, font, this->windowTitle());
		painter.setFont(font);
		painter.setPen(Qt::white);
		painter.drawText(7, 13, this->windowTitle());

		/*painter.setBrush(Qt::white);
		painter.setPen(Qt::white);
		painter.setRenderHint(QPainter::Antialiasing);
		painter.setRenderHint(QPainter::TextAntialiasing);
		painter.drawPath(path);*/

		//painter.setPen(Qt::black);
		/*painter.setBrush(Qt::white);
		painter.setRenderHint(QPainter::Antialiasing);
		painter.setRenderHint(QPainter::TextAntialiasing);*/

		

		

		//QWidget::paintEvent(e);
	}
};
