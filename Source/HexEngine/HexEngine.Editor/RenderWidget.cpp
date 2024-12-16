#include "RenderWidget.h"
#include "Inspector\Inspector.h"
#include "HexEngineEditor.h"

using namespace HexEngine;

RenderWidget::RenderWidget(QWidget* aParent, Qt::WindowFlags flags)
	: QFrame(aParent)
{
	setAttribute(Qt::WA_PaintOnScreen, true);
	setAttribute(Qt::WA_NativeWindow, true);

	installEventFilter(this);

	QWidget::setFocusPolicy(Qt::FocusPolicy::StrongFocus); // needed for key input

	//setUpdatesEnabled(false);
}

RenderWidget::~RenderWidget(void)
{
}

void RenderWidget::paintEvent(QPaintEvent* paint)
{
	if(g_pEnv)
		g_pEnv->Run();

	auto cursorPos = mapFromGlobal(QCursor::pos());

	if(_isTooling)
		g_pEditor->_toolsManager->Update(cursorPos.x(), cursorPos.y());

	update();
}

void RenderWidget::resizeEvent(QResizeEvent* event)
{
	if(g_pEnv)
		g_pEnv->OnResizeWindow(width(), height(), (HWND)winId());
}

bool RenderWidget::nativeEvent(const QByteArray& eventType, void* message_, qintptr* result)
{
	MSG* message = (MSG*)message_;

	if (g_pEnv && g_pEnv->_debugGui)
	{
		if (g_pEnv->_debugGui->Input(message->hwnd, message->message, message->wParam, message->lParam))
		{
			//return true;
		}
	}

	bool returnValue = false;
	switch (message->message)
	{
		case WM_PAINT:
		{
			bool a = false;
			break;
			//g_pEnv->Run();
			//returnValue = true;
		}
			

		case WM_INPUT:
		case WM_MOUSEMOVE:
		case WM_LBUTTONDOWN:
		case WM_LBUTTONUP:
		case WM_RBUTTONDOWN:
		case WM_RBUTTONUP:
		case WM_MBUTTONDOWN:
		case WM_MBUTTONUP:
		case WM_MOUSEWHEEL:
		case WM_XBUTTONDOWN:
		case WM_XBUTTONUP:
		case WM_MOUSEHOVER:
			dx::Mouse::ProcessMessage(message->message, message->wParam, message->lParam);
			break;

		case WM_KEYDOWN:
		case WM_SYSKEYDOWN:
		case WM_KEYUP:
		case WM_SYSKEYUP:
			dx::Keyboard::ProcessMessage(message->message, message->wParam, message->lParam);
			break;
		

		default:
			returnValue = false;
	}
	return returnValue;
}

void RenderWidget::mousePressEvent(QMouseEvent* event)
{
	if (event->button() == Qt::LeftButton)
	{
		auto pos = event->localPos();

		if (g_pEditor->_toolsManager->IsUsingTool())
		{
			_isTooling = true;
		}
		else
		{
			auto scene = HexEngine::g_pEnv->_sceneManager->GetCurrentScene();

			auto ray = HexEngine::g_pEnv->_inputSystem->GetScreenToWorldRay(scene->GetMainCamera(), pos.x(), pos.y());

			HexEngine::RayHit hit;
			if (auto didHit = scene->CameraPickEntity(ray, hit); didHit == true && hit.entity != nullptr)
			{
				gInspector.InspectEntity(hit.entity);
			}
		}
	}
	else if (event->button() == Qt::MiddleButton)
	{
		auto pos = event->localPos();
		auto topLeft = this->geometry().topLeft();
		pos -= topLeft;		

		_isAdjustingCamera = true;

		g_pEnv->_inputSystem->EnableInput(true);
		g_pEnv->_inputSystem->SetMousePosition((int)pos.x(), (int)pos.y(), true);
		g_pEnv->_inputSystem->SetMouseMode(dx::Mouse::Mode::MODE_RELATIVE);
	}
}

void RenderWidget::mouseReleaseEvent(QMouseEvent* event)
{
	if (event->button() == Qt::MiddleButton)
	{
		_isAdjustingCamera = false;
		g_pEnv->_inputSystem->EnableInput(false);
		g_pEnv->_inputSystem->SetMouseMode(dx::Mouse::Mode::MODE_ABSOLUTE);

		
	}
	else if (event->button() == Qt::LeftButton)
	{
		_isTooling = false;
	}
}

void RenderWidget::mouseMoveEvent(QMouseEvent* event)
{
	if ((event->buttons() & Qt::MiddleButton) != 0)
	{
		
	}
}

void RenderWidget::keyPressEvent(QKeyEvent* event)
{
	

	if (_isAdjustingCamera)
	{		
	}
}

void RenderWidget::render()
{
	bool a = false;
}

bool RenderWidget::eventFilter(QObject* object, QEvent* event)
{
	if (object == this && event->type() == QEvent::KeyPress)
	{
		const float CameraDebugSpeed = 1000.0f;

		if (_isAdjustingCamera)
		{
			QKeyEvent* keyEvent = (QKeyEvent*)event;

			if (keyEvent->key() == Qt::Key::Key_W)
			{
				auto camera = g_pEnv->_sceneManager->GetCurrentScene()->GetMainCamera();

				auto transform = camera->GetEntity()->GetComponent<Transform>();

				transform->SetPosition(transform->GetPosition() + (transform->GetForward() * g_pEnv->_timeManager->GetFrameTime() * CameraDebugSpeed));
			}
			else if (keyEvent->key() == Qt::Key::Key_S)
			{
				auto camera = g_pEnv->_sceneManager->GetCurrentScene()->GetMainCamera();

				auto transform = camera->GetEntity()->GetComponent<Transform>();

				transform->SetPosition(transform->GetPosition() - (transform->GetForward() * g_pEnv->_timeManager->GetFrameTime() * CameraDebugSpeed));
			}
			else if (keyEvent->key() == Qt::Key::Key_A)
			{
				auto camera = g_pEnv->_sceneManager->GetCurrentScene()->GetMainCamera();

				auto transform = camera->GetEntity()->GetComponent<Transform>();

				transform->SetPosition(transform->GetPosition() - (transform->GetRight() * g_pEnv->_timeManager->GetFrameTime() * CameraDebugSpeed));
			}
			else if (keyEvent->key() == Qt::Key::Key_D)
			{
				auto camera = g_pEnv->_sceneManager->GetCurrentScene()->GetMainCamera();

				auto transform = camera->GetEntity()->GetComponent<Transform>();

				transform->SetPosition(transform->GetPosition() + (transform->GetRight() * g_pEnv->_timeManager->GetFrameTime() * CameraDebugSpeed));
			}
		}

		return true; // lets the event continue to the edit
	}
	return false;
}