
#include "UIManager.hpp"
#include "Elements\Dialog.hpp"
#include "Elements\Dock.hpp"
#include "../Environment/IEnvironment.hpp"
#include "../Scene/SceneManager.hpp"
#include "../Environment/LogFile.hpp"

namespace HexEngine
{
	UIManager::UIManager()
	{		
	}

	UIManager::~UIManager()
	{
		g_pEnv->_inputSystem->RemoveInputListener(this);

		SAFE_UNLOAD(_blurredBackground);
		SAFE_DELETE(_blurEffect);
	}

	void UIManager::Create(uint32_t width, uint32_t height)
	{
		g_pEnv->_inputSystem->AddInputListener(this, InputEventMaskAllDesktop);

		// Set the default style
		//
		//Style::CreateDefaultStyle(Element::style);

		_width = width;
		_height = height;

		_rootElement = new Element(nullptr, Point(), Point(_width, _height));

		
	}

	void UIManager::Resize(uint32_t width, uint32_t height)
	{
		float widthScale = (float)_width / (float)width;
		float heightScale = (float)_height / (float)height ;

		ForEachElement([widthScale, heightScale](Element* element) {

			const auto& oldSize = element->GetSize();
			Point newSize;
			newSize.x = (int32_t)((float)oldSize.x * widthScale);
			newSize.y = (int32_t)((float)oldSize.y * heightScale);

			//element->SetSize(newSize);
			});

		_height = height;
		_width = width;

		if (_blurredBackground)
		{
			SAFE_DELETE(_blurredBackground);

			_blurredBackground = g_pEnv->_graphicsDevice->CreateTexture(g_pEnv->_sceneManager->GetCurrentScene()->GetMainCamera()->GetRenderTarget());
		}
	}

	void UIManager::EnableBackgroundBlur(bool enable)
	{
		_backgroundBlurEnabled = enable;
	}

	void UIManager::BlurBackbuffer()
	{
		std::vector<ITexture2D*> currentRT;

		g_pEnv->_graphicsDevice->GetRenderTargets(currentRT);

		if (_blurredBackground == nullptr)
		{
			_blurredBackground = g_pEnv->_graphicsDevice->CreateTexture(g_pEnv->_sceneManager->GetCurrentScene()->GetMainCamera()->GetRenderTarget());
		}

		_blurredBackground->ClearRenderTargetView(math::Color(0.0f));

		if (_blurEffect == nullptr)
		{
			_blurEffect = new BlurEffect(_blurredBackground, BlurType::Gaussian, 6);
		}

		GFX_PERF_BEGIN(0xFFFFFFFF, L"Background Blur");
		{
			g_pEnv->_sceneManager->GetCurrentScene()->GetMainCamera()->GetRenderTarget()->CopyTo(_blurredBackground);

			_blurEffect->Render(&_renderer, true);

			g_pEnv->_graphicsDevice->SetRenderTargets(currentRT);
		}
		GFX_PERF_END();
	}

	void UIManager::Render()
	{
		if (_backgroundBlurEnabled)
			BlurBackbuffer();

		HandleDeletions();

		//renderer->EnableScaling(true);

		RenderElement(_rootElement, GetRenderer());

		//renderer->EnableScaling(false);		
	}

	ITexture2D* UIManager::GetBlurredBackground() const
	{
		return _blurredBackground;
	}

	void UIManager::Update(float frameTime)
	{
		HandleDeletions();
	}

	void UIManager::HandleDeletions()
	{
		std::unique_lock lock(_lock);

		if (_pendingDeletion.size() > 0)
		{
			for (auto& deletion : _pendingDeletion)
			{
				if (deletion->GetParent())
					deletion->GetParent()->OnRemoveChild(deletion);

				SAFE_DELETE(deletion);
			}

			_pendingDeletion.clear();
		}
	}

	void UIManager::Lock()
	{
		_lock.lock();
	}

	void UIManager::Unlock()
	{
		_lock.unlock();
	}

	//void UIManager::HandleDeletetionImpl(Element* element)
	//{
	//	if (element->WantsDeletion())
	//	{
	//		delete element;
	//	}

	//	for (uint32_t i = 0; i < element->GetChildren().size(); ++i)
	//	{
	//		auto it = element->GetChildren().at(i);

	//		HandleDeletetionImpl(it);
	//	}
	//}

	Element* UIManager::GetRootElement() const
	{
		return _rootElement;
	}

	void UIManager::RenderElement(Element* element, GuiRenderer* renderer)
	{
		if (element->IsEnabled() == false || element->WantsDeletion() == true)
			return;

		element->PreRender(renderer, _width, _height);
		element->Render(renderer, _width, _height);	
		element->PostRender(renderer, _width, _height);

		for (auto& child : element->GetChildren())
		{
			RenderElement(child, renderer);
		}

		element->PostRenderChildren(renderer, _width, _height);
	}

	bool UIManager::OnInputEvent(InputEvent event, InputData* data)
	{
		HandleDeletions();

		return SendInputToElement(_rootElement, event, data);
	}

	bool UIManager::SendInputToElement(Element* element, InputEvent event, InputData* data)
	{
		if (element->WantsDeletion())
			return false;

		if (!element->IsEnabled())
			return true;

		std::vector<Element*> childrenReversed = element->GetChildren();
		std::reverse(childrenReversed.begin(), childrenReversed.end());

		for (auto& child : childrenReversed)
		{
			if (SendInputToElement(child, event, data) == false)
				return false;
		}

		if (element->OnInputEvent(event, data))
		{
			LOG_DEBUG("Element '%s', handled input", typeid(*element).name());
			return false;
		}

		return true;
	}

	void UIManager::ForEachElement(std::function<void(Element*)> doAction)
	{
		ForEachElementImpl(_rootElement, doAction);
	}

	void UIManager::ForEachElementImpl(Element* element, std::function<void(Element*)> doAction)
	{
		doAction(element);

		for (auto& child : element->GetChildren())
			ForEachElementImpl(child, doAction);
	}

	void UIManager::SetInputFocus(Element* element)
	{
		ForEachElement([](Element* e) {
			e->SetHasInputFocus(false);
			});

		element->SetHasInputFocus(true);
	}

	void UIManager::MarkForDeletion(Element* element)
	{
		std::unique_lock lock(_lock);

		_pendingDeletion.push_back(element);
	}

	uint32_t UIManager::GetWidth() const
	{
		return _width;
	}

	uint32_t UIManager::GetHeight() const
	{
		return _height;
	}

	GuiRenderer* UIManager::GetRenderer()
	{
		return &_renderer;
	}
}