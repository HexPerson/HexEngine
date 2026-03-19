

#pragma once

#include "../Required.hpp"
#include "../Entity/Component/Camera.hpp"

namespace HexEngine
{
	enum class InputEvent : uint32_t
	{
		KeyDown				= HEX_BITSET(0),
		KeyUp				= HEX_BITSET(1),
		MouseDown			= HEX_BITSET(2),
		MouseDoubleClick	= HEX_BITSET(3),
		MouseUp				= HEX_BITSET(4),
		MouseWheel			= HEX_BITSET(5),
		MouseMove			= HEX_BITSET(6),
		Char				= HEX_BITSET(7),
		DragAndDrop			= HEX_BITSET(8)
	};

	DEFINE_ENUM_FLAG_OPERATORS(InputEvent);

#define InputEventMaskAllDesktop (HexEngine::InputEvent::KeyDown | HexEngine::InputEvent::KeyUp | HexEngine::InputEvent::MouseDown | HexEngine::InputEvent::MouseDoubleClick | HexEngine::InputEvent::MouseUp | HexEngine::InputEvent::MouseWheel | HexEngine::InputEvent::MouseMove | HexEngine::InputEvent::Char | HexEngine::InputEvent::DragAndDrop)

	struct InputData
	{
		union
		{
			struct
			{
				int32_t key;

			} KeyDown;

			struct
			{
				int32_t key;
			} KeyUp;

			struct
			{
				int32_t button;
				int32_t xpos;
				int32_t ypos;

			} MouseDown;

			struct
			{
				int32_t button;
				int32_t xpos;
				int32_t ypos;

			} MouseUp;

			struct
			{
				float delta;

			} MouseWheel;

			struct
			{
				float x;
				float y;
				bool absolute;
			} MouseMove;

			struct
			{
				wchar_t ch;
			} Char;

			struct
			{
				wchar_t path[MAX_PATH];
				int32_t x;
				int32_t y;
			} DragAndDrop;
		};
	};

	class IInputListener
	{
	public:
		virtual bool OnInputEvent(InputEvent event, InputData* data) = 0;
	};

	enum class MouseLockMode
	{		
		Free,
		Constrained,
		Locked, // FPS style
	};

	class HEX_API InputSystem
	{
	public:
		~InputSystem();

		void Create(HWND handle);

		LRESULT HandleWindowMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

		void EnableRawInput(bool enable);

		void EnableMouseSmoothing(bool enable);

		void OnKeyDown(int32_t key);

		void OnKeyUp(int32_t key);

		void OnMouseDown(int32_t key);

		void OnMouseDoubleClick(int32_t key);

		void OnMouseUp(int32_t key);

		void OnMouseWheel(float delta);

		void OnMouseMove(float x, float y, bool absolute);

		void OnChar(wchar_t ch);

		void OnDragAndDropFiles(const std::vector<std::wstring>& files, int32_t x, int32_t y);

		void AddInputListener(IInputListener* listener, InputEvent events);

		void RemoveInputListener(IInputListener* listener);

		math::Vector3 GetScreenToWorldRay(Camera* camera, int32_t mouseX, int32_t mouseY);

		math::Vector3 GetScreenToWorldRay(Camera* camera, int32_t screenX, int32_t screenY, int32_t screenWidth, int32_t screenHeight);

		math::Vector3 GetScreenToWorldPosition(Camera* camera, int mouseX, int mouseY);

		bool GetWorldToScreenPosition(Camera* camera, const math::Vector3& position, int32_t& x, int32_t& y, int32_t width = 0, int32_t height = 0);

		void GetMousePosition(int& x, int& y);

		void SetMousePosition(int x, int y, bool absolute = false);

		void EnableInput(bool enable);

		void ShowCursor(bool show);

		//void SetMouseMode(dx::Mouse::Mode mode);

		bool IsInputEnabled() { return _inputEnabled; }

		bool IsMouseCursorVisible() { return _mouseCursorVisible; }

		void FireEvent(InputEvent event, InputData* data);

		void Update(float frameTime);

		void SetMouseLockMode(MouseLockMode mode);

		float GetXAxis() const;
		float GetYAxis() const;

		void SetInputViewport(int32_t x, int32_t y, int32_t w, int32_t h);

		bool IsCtrlDown() const;

	private:
		void ProcessMouseMovement(float frameTime);

	private:
		HWND _targetWnd = NULL;
		std::vector<std::pair<InputEvent, IInputListener*>> _listeners;
		int _mouseX = 0;
		int _mouseY = 0;
		int _lastMouseX = 0;
		int _lastMouseY = 0;
		bool _inputEnabled = true;
		bool _mouseCursorVisible = false;
		bool _rawInputEnabled = false;
		bool _rawInputDevicesCreated = false;
		//bool _mouseSmoothingEnabled = true;

		std::vector<std::pair<float, float>> _smoothedMouseMovement;
		MouseLockMode _mouseLockMode = MouseLockMode::Free;

		bool _isInInputLoop = false;
		std::vector<std::pair<InputEvent, IInputListener*>> _pendingListeners;
		std::vector<IInputListener*> _pendingRemoveListeners;

		float _inputAxis[2] = { 0.0f };
		bool _interruptInputImmediately = false;

		math::Viewport _vp;
		bool _hasCustomVP = false;
	};
}
