
#pragma once

#include <QWidget>
#include <qframe.h>
#include <qevent.h>

#define DISABLE_MEM_TRACKING 1
#include <HexEngine.Core/Environment/Game3DEnvironment.hpp>
#define DISABLE_MEM_TRACKING 0

class RenderWidget : public QFrame
{
	Q_OBJECT
	Q_DISABLE_COPY(RenderWidget)

public:
	RenderWidget(QWidget* aParent = NULL, Qt::WindowFlags flags = (Qt::WindowFlags)0);
	~RenderWidget(void);

	virtual QPaintEngine* paintEngine() const override
	{
		return NULL;
	}

protected:
	void paintEvent(QPaintEvent*);
	void resizeEvent(QResizeEvent*);
	virtual bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;
	virtual void mousePressEvent(QMouseEvent* event) override;
	virtual void mouseReleaseEvent(QMouseEvent* event) override;
	virtual void mouseMoveEvent(QMouseEvent* event) override;
	virtual void keyPressEvent(QKeyEvent* event) override;
	virtual bool eventFilter(QObject* object, QEvent* event) override;
	void render(void);

private:
	bool _isAdjustingCamera = false;
	bool _isTooling = false;
};