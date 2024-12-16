//
//
//#pragma once
//
//#include "../Required.hpp"
//#include "../Entity/Camera.hpp"
//#include "../SAL/SAL.hpp"
//
//namespace HexEngine
//{
//	enum class InputEvent : uint32_t
//	{
//		KeyDown = BITSET(0),
//		KeyUp = BITSET(1),
//		MouseDown = BITSET(2),
//		MouseUp = BITSET(3),
//		MouseWheel = BITSET(4),
//		MouseMove = BITSET(5)
//	};
//
//	DEFINE_ENUM_FLAG_OPERATORS(InputEvent);
//
//#define InputEventMaskAllDesktop (InputEvent::KeyDown | InputEvent::KeyUp | InputEvent::MouseDown | InputEvent::MouseUp | InputEvent::MouseWheel | InputEvent::MouseMove)
//
//	struct InputData
//	{
//		union
//		{
//			struct 
//			{
//				int32_t key;
//
//			} KeyDown;
//
//			struct 
//			{
//				int32_t key;
//			} KeyUp;
//
//			struct
//			{
//				int32_t button;
//				int32_t xpos;
//				int32_t ypos;
//
//			} MouseDown;
//
//			struct
//			{
//				int32_t button;
//				int32_t xpos;
//				int32_t ypos;
//
//			} MouseUp;
//
//			struct
//			{
//				float delta;
//
//			} MouseWheel;
//
//			struct
//			{
//				int32_t x;
//				int32_t y;
//			} MouseMove;
//
//		};
//	};
//
//	class IInputListener
//	{
//	public:
//		virtual void OnInputEvent(InputEvent event, InputData* data) = 0;
//
//		void EnableInput();
//		void DisableInput();
//	};
//
//	class IInputSystem
//	{
//	public:
//		virtual void Create(WinHandle handle) = 0;
//
//		virtual void OnKeyDown(int32_t key) = 0;
//
//		virtual void OnKeyUp(int32_t key) = 0;
//
//		virtual void OnMouseDown(int32_t key, int32_t xPos, int32_t yPos) = 0;
//
//		virtual void OnMouseUp(int32_t key, int32_t xPos, int32_t yPos) = 0;
//
//		virtual void OnMouseWheel(float delta) = 0;
//
//		virtual void OnMouseMove(int x, int y) = 0;
//
//		virtual void AddInputListener(IInputListener* listener, InputEvent events) = 0;
//
//		virtual void RemoveInputListener(IInputListener* listener) = 0;
//
//		virtual math::Vector3 GetScreenToWorldRay(Camera* camera, int mouseX, int mouseY) = 0;
//
//		virtual math::Vector3 GetScreenToWorldPosition(Camera* camera, int mouseX, int mouseY) = 0;
//
//		virtual void GetMousePosition(int& x, int& y) = 0;
//
//		virtual void SetMousePosition(int x, int y, bool absolute=false) = 0;
//
//		virtual void EnableInput(bool enable) = 0;
//
//		virtual void Update() = 0;
//	};
//}
