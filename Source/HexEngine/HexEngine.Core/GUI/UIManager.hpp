
#pragma once

#include "../Required.hpp"
#include "Elements\Element.hpp"
#include "Elements\MenuBar.hpp"
#include "Elements\TreeList.hpp"
#include "GuiRenderer.hpp"
#include "../Graphics/BlurEffect.hpp"

namespace HexEngine
{
	class HEX_API UIManager : public IInputListener
	{
	public:
		UIManager();
		~UIManager();

		virtual void Create(uint32_t width, uint32_t height);
		virtual void Render();
		virtual void Update(float frameTime);

		void Resize(uint32_t width, uint32_t height);

		virtual bool OnInputEvent(InputEvent event, InputData* data) override;

		void SetInputFocus(Element* element);
		Element* GetInputFocus() const;
		void ForEachElement(std::function<void(Element*)> doAction);

		void MarkForDeletion(Element* element);

		uint32_t GetWidth() const;
		uint32_t GetHeight() const;
		GuiRenderer* GetRenderer();

		void EnableBackgroundBlur(bool enable);
		ITexture2D* GetBlurredBackground() const;

		Element* GetRootElement() const;		
		void Lock();
		void Unlock();

	private:
		void RenderElement(Element* element, GuiRenderer* renderer);
		bool SendInputToElement(Element* element, InputEvent event, InputData* data);
		
		void ForEachElementImpl(Element* element, std::function<void(Element*)> doAction);

		void HandleDeletions();
		void HandleDeletetionImpl(Element* element);		
		void BlurBackbuffer();

	protected:
		Element* _rootElement;
		uint32_t _width = 0;
		uint32_t _height = 0;

		std::vector<Element*> _pendingDeletion;

	private:
		GuiRenderer _renderer;
		ITexture2D* _blurredBackground = nullptr;
		BlurEffect* _blurEffect = nullptr;
		bool _backgroundBlurEnabled = false;
		std::recursive_mutex _lock;
		Element* _inputFocs = nullptr;
	};
}
