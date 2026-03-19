

#include "InputSystem.hpp"
#include "../Entity/Component/Transform.hpp"
#include "../HexEngine.hpp"

namespace HexEngine
{
	HVar in_pitchSensitivity("in_pitchSensitivity", "The mouse input sensitivity on the pitch axis", 1.0f, 0.01f, 10.0f);
	HVar in_yawSensitivity("in_yawSensitivity", "The mouse input sensitivity on the yaw axis", 1.0f, 0.01f, 10.0f);
	HVar in_mouseSmoothing("in_mouseSmoothing", "Enable or disable mouse smoothing", true, false, true);
	HVar in_mouseSmoothingWeight("in_mouseSmoothingWeight", "The weighted mouse smoothing value, a higher value will result in smoother but less responsive movement. A lower value will result in snappier movements but less smooth", 0.6f, 0.001f, 1.0f);
	HVar in_mouseSmoothingSamples("in_mouseSmoothingSamples", "The number of samples to use for mouse smoothing", 32, 2, 64);

	void InputSystem::Create(HWND handle)
	{
		_targetWnd = handle;
	}

	InputSystem::~InputSystem()
	{
		ShowCursor(true);
	}

	void InputSystem::EnableRawInput(bool enable)
	{
		if (enable)
		{
			if (_rawInputEnabled == false)
			{
				if (_rawInputDevicesCreated == false)
				{
					RAWINPUTDEVICE Rid[2];

					Rid[0].usUsagePage = HID_USAGE_PAGE_GENERIC;
					Rid[0].usUsage = HID_USAGE_GENERIC_MOUSE;
					Rid[0].dwFlags = 0;// RIDEV_NOLEGACY;    // adds mouse and also ignores legacy mouse messages
					Rid[0].hwndTarget = _targetWnd;

					Rid[1].usUsagePage = HID_USAGE_PAGE_GENERIC;          // HID_USAGE_PAGE_GENERIC
					Rid[1].usUsage = HID_USAGE_GENERIC_KEYBOARD;              // HID_USAGE_GENERIC_KEYBOARD
					Rid[1].dwFlags = 0;// RIDEV_NOLEGACY;    // adds keyboard and also ignores legacy keyboard messages
					Rid[1].hwndTarget = _targetWnd;

					if (RegisterRawInputDevices(Rid, 2, sizeof(Rid[0])) == FALSE)
					{
						LOG_CRIT("Could not register raw input devices. Error: %d", GetLastError());
						return;
					}

					_rawInputDevicesCreated = true;
				}

				_rawInputEnabled = true;
			}
		}
		else
		{
			_rawInputEnabled = false;
		}
	}

	/*void InputSystem::EnableMouseSmoothing(bool enable)
	{
		_mouseSmoothingEnabled = enable;
	}*/

	LRESULT InputSystem::HandleWindowMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
#if 0//def _DEBUG
		if (auto text = GetMessageText(message); text != nullptr)
		{
			LOG_DEBUG("Message: %x (%S)", message, text);
		}
#endif

		Window* win = Window::FindFromHandle(hWnd);

		//Window* window = reinterpret_cast<Window*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));

		switch (message)
		{
		case WM_NCCREATE:
			//ShowUsedMessages();
			//SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCT*)lParam)->lpCreateParams);
			break;

		case WM_DESTROY:
		{
			PostQuitMessage(0);
			return 0;
		}

		case WM_KILLFOCUS:
		{
			if (g_pEnv)
			{
				EnableInput(false);
				g_pEnv->SetHasFocus(false);
			}

			//ShowCursor(true);
			break;// return 0;
		}

		case WM_SETFOCUS:
		{
			if (g_pEnv)
			{
				EnableInput(true);
				g_pEnv->SetHasFocus(true);
			}

			//ShowCursor(false);

			break;// return 0;
		}

		case WM_SIZE:
		{
			//RECT rect;
			//GetClientRect(hWnd, &rect);



			if (g_pEnv)
			{
				//g_pEnv->OnResizeWindow(rect.right - rect.left, rect.bottom - rect.top, hWnd);
				g_pEnv->OnResizeWindow(win, LOWORD(lParam), HIWORD(lParam));
			}
			break;
		}

		case WM_WINDOWPOSCHANGED:
		{
			break;
			WINDOWPOS* wp = (WINDOWPOS*)lParam;

			if (g_pEnv && (wp->flags & SWP_NOSIZE) != SWP_NOSIZE)
			{
				//RECT rect;
				//GetClientRect(hWnd, &rect);
				//g_pEnv->OnResizeWindow(rect.right - rect.left, rect.bottom - rect.top);
				g_pEnv->OnResizeWindow(win, wp->cx, wp->cy);
			}

			break;
		}

		case WM_DROPFILES:
		{
			HDROP drop = (HDROP)wParam;

			POINT dragPos;
			DragQueryPoint(drop, &dragPos);

			UINT numFiles = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);

			std::vector<std::wstring> files;

			for (UINT i = 0; i < numFiles; ++i)
			{
				wchar_t fileName[MAX_PATH];
				DragQueryFileW(drop, i, fileName, _countof(fileName));

				files.push_back(fileName);
			}

			OnDragAndDropFiles(files, dragPos.x, dragPos.y);
			DragFinish(drop);

			break;

		}

		/*case WM_ACTIVATEAPP:
			dx::Mouse::ProcessMessage(message, wParam, lParam);
			dx::Keyboard::ProcessMessage(message, wParam, lParam);
			break;

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
			dx::Mouse::ProcessMessage(message, wParam, lParam);
			break;

		case WM_KEYDOWN:
		case WM_SYSKEYDOWN:
		case WM_KEYUP:
		case WM_SYSKEYUP:
			dx::Keyboard::ProcessMessage(message, wParam, lParam);
			break;*/

#if 1
		case WM_KEYDOWN:
		case WM_SYSKEYDOWN:
			//if(_rawInputEnabled == false)
				OnKeyDown((int32_t)wParam);
			break;

		case WM_KEYUP:
		case WM_SYSKEYUP:
			//if (_rawInputEnabled == false)
				OnKeyUp((int32_t)wParam);
			break;
#endif

		case WM_LBUTTONDOWN:
			if (_rawInputEnabled == false)
				OnMouseDown(VK_LBUTTON);
			break;

		case WM_LBUTTONDBLCLK:
			if (_rawInputEnabled == false)
				OnMouseDoubleClick(VK_LBUTTON);
			break;

		case WM_LBUTTONUP:
			if (_rawInputEnabled == false)
				OnMouseUp(VK_LBUTTON);
			break;

		case WM_RBUTTONDOWN:
			if (_rawInputEnabled == false)
				OnMouseDown(VK_RBUTTON);
			break;

		case WM_RBUTTONDBLCLK:
			if (_rawInputEnabled == false)
				OnMouseDoubleClick(VK_RBUTTON);
			break;

		case WM_RBUTTONUP:
			if (_rawInputEnabled == false)
				OnMouseUp(VK_RBUTTON);
			break;

		case WM_MBUTTONDOWN:
			if (_rawInputEnabled == false)
				OnMouseDown(VK_MBUTTON);
			break;

		case WM_MBUTTONUP:
			if (_rawInputEnabled == false)
				OnMouseUp(VK_MBUTTON);
			break;

		case WM_XBUTTONDOWN:
			if (_rawInputEnabled == false)
				OnMouseDown(VK_XBUTTON1 + (GET_XBUTTON_WPARAM(wParam) - 1));
			break;

		case WM_XBUTTONUP:
			if (_rawInputEnabled == false)
				OnMouseUp(VK_XBUTTON1 + (GET_XBUTTON_WPARAM(wParam) - 1));
			break;

		case WM_MOUSEWHEEL:
			if (_rawInputEnabled == false)
			{
				int16_t delta = HIWORD(wParam);

				OnMouseWheel((float)(delta / WHEEL_DELTA));
			}
			break;

		case WM_MOUSEMOVE:
		{
			if (_rawInputEnabled == false)
			{
				POINTS p = MAKEPOINTS(lParam);
				POINT point;
				point.x = p.x;
				point.y = p.y;
				//int16_t mX = LOWORD(lParam);
				//int16_t mY = HIWORD(lParam);
				//ClientToScreen(hWnd, &point);
				SetMousePosition(point.x, point.y, true);
			}
			break;
		}

		case WM_CHAR:
		{
			OnChar((wchar_t)wParam);
			break;
		}

		case WM_INPUT:
		{
			if (_rawInputEnabled)
			{
				UINT bufferSize;
				GetRawInputData((HRAWINPUT)lParam, RID_INPUT, NULL, &bufferSize, sizeof(RAWINPUTHEADER));
				BYTE* buffer = new BYTE[bufferSize];
				GetRawInputData((HRAWINPUT)lParam, RID_INPUT, (LPVOID)buffer, &bufferSize, sizeof(RAWINPUTHEADER));

				RAWINPUT* raw = (RAWINPUT*)buffer;

				if (raw->header.dwType == RIM_TYPEMOUSE)
				{
					SetMousePosition(raw->data.mouse.lLastX, raw->data.mouse.lLastY, false);

					int32_t mouseX, mouseY;
					GetMousePosition(mouseX, mouseY);

					switch (raw->data.mouse.usButtonFlags)
					{
					case RI_MOUSE_LEFT_BUTTON_DOWN:
						OnMouseDown(VK_LBUTTON);
						break;

					case RI_MOUSE_LEFT_BUTTON_UP:
						OnMouseUp(VK_LBUTTON);
						break;

					case RI_MOUSE_RIGHT_BUTTON_DOWN:
						OnMouseDown(VK_RBUTTON);
						break;

					case RI_MOUSE_RIGHT_BUTTON_UP:
						OnMouseUp(VK_RBUTTON);
						break;

					case RI_MOUSE_MIDDLE_BUTTON_DOWN:
						OnMouseDown(VK_MBUTTON);
						break;

					case RI_MOUSE_MIDDLE_BUTTON_UP:
						OnMouseUp(VK_MBUTTON);
						break;

					case RI_MOUSE_BUTTON_4_DOWN:
						OnMouseDown(VK_XBUTTON1);
						break;

					case RI_MOUSE_BUTTON_4_UP:
						OnMouseUp(VK_XBUTTON1);
						break;

					case RI_MOUSE_BUTTON_5_DOWN:
						OnMouseDown(VK_XBUTTON2);
						break;

					case RI_MOUSE_BUTTON_5_UP:
						OnMouseUp(VK_XBUTTON2);
						break;

					case RI_MOUSE_WHEEL:
						const float mouseWheelDelta = (float)((short)LOWORD(raw->data.mouse.usButtonData));
						OnMouseWheel(mouseWheelDelta / (float)WHEEL_DELTA);
						break;
					}
				}
#if 0
				else if (raw->header.dwType == RIM_TYPEKEYBOARD)
				{
					switch (raw->data.keyboard.Message)
					{
					case WM_SYSKEYDOWN:
					case WM_KEYDOWN:
						OnKeyDown((int32_t)raw->data.keyboard.VKey);
						break;

					case WM_SYSKEYUP:
					case WM_KEYUP:
						OnKeyUp((int32_t)raw->data.keyboard.VKey);
						break;
					}
				}
#endif

				SAFE_DELETE_ARRAY(buffer);
			}
			return 0;
		}

		case WM_CLOSE:
		{
			PostQuitMessage(0);
			return 0;
		}
		}

		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	void InputSystem::OnKeyDown(int32_t key)
	{
		InputData data;
		data.KeyDown.key = key;

		FireEvent(InputEvent::KeyDown, &data);
	}

	void InputSystem::OnKeyUp(int32_t key)
	{
		InputData data;
		data.KeyUp.key = key;

		FireEvent(InputEvent::KeyUp, &data);
	}

	void InputSystem::OnMouseDown(int32_t key)
	{
		InputData data;
		data.MouseDown.button = key;		

		POINT p = { _mouseX, _mouseY };
		//ScreenToClient(_targetWnd, &p);

		data.MouseDown.xpos = p.x;
		data.MouseDown.ypos = p.y;

		FireEvent(InputEvent::MouseDown, &data);
	}

	void InputSystem::OnMouseDoubleClick(int32_t key)
	{
		InputData data;
		data.MouseDown.button = key;
		data.MouseDown.xpos = _mouseX;
		data.MouseDown.ypos = _mouseY;

		FireEvent(InputEvent::MouseDoubleClick, &data);
	}

	void InputSystem::OnMouseUp(int32_t key)
	{
		InputData data;
		data.MouseUp.button = key;
		data.MouseUp.xpos = _mouseX;
		data.MouseUp.ypos = _mouseY;

		FireEvent(InputEvent::MouseUp, &data);
	}

	void InputSystem::OnMouseWheel(float delta)
	{
		InputData data;
		data.MouseWheel.delta = delta;

		FireEvent(InputEvent::MouseWheel, &data);
	}

	void InputSystem::OnMouseMove(float x, float y, bool absolute)
	{
		// no need to send if we have no movement (this can happen with smoothed mouse input)
		if (x != 0.0f && y != 0.0f)
		{
			InputData data;
			data.MouseMove.x = x;
			data.MouseMove.y = y;
			data.MouseMove.absolute = absolute;

			FireEvent(InputEvent::MouseMove, &data);
		}
	}

	void InputSystem::OnChar(wchar_t ch)
	{
		InputData data;
		data.Char.ch = ch;

		FireEvent(InputEvent::Char, &data);
	}

	void InputSystem::OnDragAndDropFiles(const std::vector<std::wstring>& files, int32_t x, int32_t y)
	{
		for (auto& file : files)
		{
			InputData data;
			wcscpy_s(data.DragAndDrop.path, file.c_str());
			data.DragAndDrop.x = x;
			data.DragAndDrop.y = y;

			FireEvent(InputEvent::DragAndDrop, &data);
		}
	}

	void InputSystem::AddInputListener(IInputListener* listener, InputEvent events)
	{
		if (_isInInputLoop)
			_pendingListeners.push_back({ events, listener });
		else
			_listeners.push_back({ events, listener });
	}

	void InputSystem::RemoveInputListener(IInputListener* listener)
	{
		if (_isInInputLoop)
		{
			_pendingRemoveListeners.push_back(listener);
			_interruptInputImmediately = true;
		}
		else
		{
			for (auto it = _listeners.begin(); it != _listeners.end(); it++)
			{
				if (it->second == listener)
				{
					_listeners.erase(it);
					break;
				}
			}
		}
	}

	void InputSystem::FireEvent(InputEvent event, InputData* data)
	{
		// We have to allow drag and drop through because we might lose focus whilst browing for files
		//
		if (!_inputEnabled && event != InputEvent::DragAndDrop)
			return;

		// don't run any input events whilst we're waiting for listeners to delete, otherwise we will probably crash :o
		if (_pendingRemoveListeners.size() > 0)
			return;

		_isInInputLoop = true;

		for (auto&& listener : _listeners)
		{
			if (HEX_HASFLAG(listener.first, event))
			{
				listener.second->OnInputEvent(event, data);				
			}
			if (_interruptInputImmediately)
				break;
		}

		_isInInputLoop = false;
		_interruptInputImmediately = false;
	}

	math::Vector3 InputSystem::GetScreenToWorldRay(Camera* camera, int32_t screenX, int32_t screenY)
	{
		uint32_t width, height;
		g_pEnv->GetScreenSize(width, height);

		return GetScreenToWorldRay(camera, screenX, screenY, width, height);
	}

	bool InputSystem::IsCtrlDown() const
	{
		return (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
	}

	math::Vector3 InputSystem::GetScreenToWorldRay(Camera* camera, int32_t screenX, int32_t screenY, int32_t screenWidth, int32_t screenHeight)
	{
		math::Vector3 start_point, end_point;

		// begin the ray right where the camera is at (in world space)
		start_point = camera->GetEntity()->GetPosition();

		uint32_t width, height;
		g_pEnv->GetScreenSize(width, height);

		// first, set the end point to the screen space position on the far plane
		end_point.x = ((2.0f * (float)screenX) / (float)screenWidth) - 1.0f;
		end_point.y = (((2.0f * (float)screenY) / (float)screenHeight) - 1.0f) * -1.0f;
		end_point.z = camera->GetNearZ();// 1.0f;

		auto projectionMatrix = camera->GetProjectionMatrix();

		auto end_point2 = end_point;

		end_point2.x /= projectionMatrix._11;
		end_point2.y /= projectionMatrix._22;

		auto viewMatrix = camera->GetViewMatrix();
		math::Matrix viewMatrixInverse = viewMatrix.Invert();

		//// convert picks into world space
		//math::Vector3 rayOrigin, rayDirection(end_point);
		//rayOrigin = math::Vector3::Transform(rayOrigin, viewMatrixInverse);
		//rayDirection = math::Vector3::TransformNormal(rayDirection, viewMatrixInverse);

		// Calculate the direction of the picking ray in view space.
		math::Vector3 direction;
		direction.x = (end_point2.x * viewMatrixInverse._11) + (end_point2.y * viewMatrixInverse._21) + viewMatrixInverse._31;
		direction.y = (end_point2.x * viewMatrixInverse._12) + (end_point2.y * viewMatrixInverse._22) + viewMatrixInverse._32;
		direction.z = (end_point2.x * viewMatrixInverse._13) + (end_point2.y * viewMatrixInverse._23) + viewMatrixInverse._33;

		// take the inverse of the view * projection matrix
		auto inverse_view_projection_matrix = (viewMatrix * projectionMatrix);
		inverse_view_projection_matrix = inverse_view_projection_matrix.Invert();// math.inverse_matrix(&inverse_view_projection_matrix, 0.0f);

		end_point = math::Vector3::Transform(end_point, inverse_view_projection_matrix);
		// multiply the screen-space position on the far plane with the inverse view projection matrix to bring the end point from screen to world space
		//end_point. *= inverse_view_projection_matrix;

		// now we only have to create our ray (vector) from the two points
		auto dir = (end_point - start_point);
		dir.Normalize();

		return dir;// dir;
	}

	math::Vector3 InputSystem::GetScreenToWorldPosition(Camera* camera, int screenX, int screenY)
	{
		math::Vector3 start_point, end_point;

		// begin the ray right where the camera is at (in world space)
		start_point = camera->GetEntity()->GetPosition();

		const auto& viewport = camera->GetViewport();

		// first, set the end point to the screen space position on the far plane
		end_point.x = ((2.0f * (float)screenX) / (float)viewport.width) - 1.0f;
		end_point.y = (((2.0f * (float)screenY) / (float)viewport.height) - 1.0f) * -1.0f;
		end_point.z = camera->GetNearZ();// 1.0f;

		auto projectionMatrix = camera->GetProjectionMatrix();
		auto viewMatrix = camera->GetViewMatrix();

		//auto end_point2 = end_point;

		//end_point2.x /= projectionMatrix._11;
		//end_point2.y /= projectionMatrix._22;

		//
		//math::Matrix viewMatrixInverse = viewMatrix.Invert();

		////// convert picks into world space
		////math::Vector3 rayOrigin, rayDirection(end_point);
		////rayOrigin = math::Vector3::Transform(rayOrigin, viewMatrixInverse);
		////rayDirection = math::Vector3::TransformNormal(rayDirection, viewMatrixInverse);

		//// Calculate the direction of the picking ray in view space.
		//math::Vector3 direction;
		//direction.x = (end_point2.x * viewMatrixInverse._11) + (end_point2.y * viewMatrixInverse._21) + viewMatrixInverse._31;
		//direction.y = (end_point2.x * viewMatrixInverse._12) + (end_point2.y * viewMatrixInverse._22) + viewMatrixInverse._32;
		//direction.z = (end_point2.x * viewMatrixInverse._13) + (end_point2.y * viewMatrixInverse._23) + viewMatrixInverse._33;

		// take the inverse of the view * projection matrix
		auto inverse_view_projection_matrix = (viewMatrix * projectionMatrix);
		inverse_view_projection_matrix = inverse_view_projection_matrix.Invert();// math.inverse_matrix(&inverse_view_projection_matrix, 0.0f);

		end_point = math::Vector3::Transform(end_point, inverse_view_projection_matrix);
		// multiply the screen-space position on the far plane with the inverse view projection matrix to bring the end point from screen to world space
		//end_point. *= inverse_view_projection_matrix;

		return end_point;
	}

	bool InputSystem::GetWorldToScreenPosition(Camera* camera, const math::Vector3& position, int32_t& x, int32_t& y, int32_t width, int32_t height)
	{
		if (_hasCustomVP)
		{
			width = _vp.width;
			height = _vp.height;
		}
		auto projectionMatrix = camera->GetProjectionMatrix();
		auto viewMatrix = camera->GetViewMatrix();

		auto viewProj = viewMatrix * projectionMatrix;

		const math::Matrix world = math::Matrix::CreateWorld(position, math::Vector3::Forward, math::Vector3::Up);

		auto screenVec = math::Vector4::Transform(math::Vector4(position.x, position.y, position.z, 1.0f), viewProj);

		if (screenVec.z < 0.001f)
			return false;

		screenVec.x /= screenVec.w;
		screenVec.y /= screenVec.w;
		screenVec.z /= screenVec.w;		

		if (width == 0)
			width = camera->GetViewport().width;
		if (height == 0)
			height = camera->GetViewport().height;

		x = (int32_t)((screenVec.x + 1.0f) * ((float)width * 0.5f));
		y = (int32_t)((screenVec.y + 1.0f) * ((float)height * 0.5f));

		y = height - y;

		if (x < 0 || x > width)
			return false;

		if (y < 0 || y > height)
			return false;

		if (_hasCustomVP)
		{
			x += _vp.x;
			y += _vp.y;
		}

		return true;
	}

	void InputSystem::GetMousePosition(int& x, int& y)
	{
		x = _mouseX;
		y = _mouseY;
	}

	void InputSystem::SetMousePosition(int x, int y, bool absolute)
	{
		if (!_inputEnabled)
			return;

		if (absolute)
		{
			_mouseX = x;
			_mouseY = y;
		}
		else
		{
			_mouseX += x;
			_mouseY += y;
		}

		OnMouseMove((float)_mouseX, (float)_mouseY, true);		
	}

	void InputSystem::EnableInput(bool enable)
	{
		_inputEnabled = enable;
	}

	void InputSystem::ShowCursor(bool show)
	{
		if (show == false)
		{
			while (::ShowCursor(show) >= 0) {}
		}
		else
		{
			while (::ShowCursor(show) < 0) {}
		}
	}

	/*void InputSystem::SetMouseMode(dx::Mouse::Mode mode)
	{
		_mouse->SetMode(mode);
	}*/

	void InputSystem::Update(float frameTime)
	{
		if (_isInInputLoop == false)
		{
			if (_pendingListeners.size() > 0)
			{
				_listeners.insert(_listeners.end(), _pendingListeners.begin(), _pendingListeners.end());
				_pendingListeners.clear();
			}

			if (_pendingRemoveListeners.size() > 0)
			{
				for (auto& remove : _pendingRemoveListeners)
				{
					_listeners.erase(std::remove_if(_listeners.begin(), _listeners.end(),
						[remove](const std::pair<InputEvent, IInputListener*>& pair)
						{
							return pair.second == remove;
						}), _listeners.end());
				}
				_pendingRemoveListeners.clear();
			}
		}
		//if (_smoothedMouseMovement.size() > 0)
		//	_smoothedMouseMovement.erase(_smoothedMouseMovement.begin());

		if(_mouseLockMode == MouseLockMode::Locked)
			ProcessMouseMovement(frameTime);

		//auto keyboardState = _keyboard->GetState();

		uint32_t width, height;
		g_pEnv->GetScreenSize(width, height);

		if (g_pEnv->GetHasFocus() && g_pEnv->IsEditorMode() == false && _mouseLockMode == MouseLockMode::Locked)
		{
			SetCursorPos(width / 2, height / 2);

			_mouseX = width / 2;
			_mouseY = height / 2;
		}
	}

	void InputSystem::ProcessMouseMovement(float frameTime)
	{
		// Clear out any that are too old
		//
		if (_smoothedMouseMovement.size() > in_mouseSmoothingSamples._val.i32)
		{
			_smoothedMouseMovement.pop_back();
		}

		uint32_t width, height;
		g_pEnv->GetScreenSize(width, height);

		float deltaX = (float)(_mouseX - (float)(width >> 1));// *frameTime;
		float deltaY = (float)(_mouseY - (float)(height >> 1));// *frameTime;

		if (deltaX != 0.0f || deltaY != 0.0f)
		{
			float mouseX = 0.0f;
			float mouseY = 0.0f;

			if (in_mouseSmoothing._val.b)
			{
				_smoothedMouseMovement.insert(_smoothedMouseMovement.begin(), { deltaX, deltaY });


				float mouseTotalYaw = 0.0f;
				float mouseTotalPitch = 0.0f;

				const float weightModifier = in_mouseSmoothingWeight._val.f32;
				float sampleWeight = 1.0f;

				for (auto i = 0UL; i < _smoothedMouseMovement.size(); ++i)
				{
					auto& history = _smoothedMouseMovement.at(i);

					mouseTotalYaw += history.first * sampleWeight;
					mouseTotalPitch += history.second * sampleWeight;

					sampleWeight *= weightModifier;
				}
				mouseX = mouseTotalYaw / (float)_smoothedMouseMovement.size();
				mouseY = mouseTotalPitch / (float)_smoothedMouseMovement.size();

				
			}
			else
			{
				mouseX = (float)deltaX;
				mouseY = (float)deltaY;
			}

			OnMouseMove(mouseX * in_yawSensitivity._val.f32, mouseY * in_pitchSensitivity._val.f32, false);

			_inputAxis[0] = mouseX * in_yawSensitivity._val.f32;
			_inputAxis[1] = mouseY * in_yawSensitivity._val.f32;

			_lastMouseX = _mouseX;
			_lastMouseY = _mouseY;
		}
	}

	float InputSystem::GetXAxis() const
	{
		return _inputAxis[0];
	}

	float InputSystem::GetYAxis() const
	{
		return _inputAxis[1];
	}

	void InputSystem::SetMouseLockMode(MouseLockMode mode)
	{
		_mouseLockMode = mode;

		switch (mode)
		{
		case MouseLockMode::Locked:
			ShowCursor(false);

			uint32_t width, height;
			g_pEnv->GetScreenSize(width, height);
			SetCursorPos(width / 2, height / 2);

			_mouseX = width / 2;
			_mouseY = height / 2;

			EnableRawInput(true);

			_smoothedMouseMovement.clear();
			break;

		case MouseLockMode::Constrained:
			EnableRawInput(false);
			ShowCursor(true);
			break;

		case MouseLockMode::Free:
			EnableRawInput(false);
			ShowCursor(true);
			break;
		}
	}

	void InputSystem::SetInputViewport(int32_t x, int32_t y, int32_t w, int32_t h)
	{
		_vp.x = x;
		_vp.y = y;
		_vp.width = w;
		_vp.height = h;
		_hasCustomVP = true;
	}
}