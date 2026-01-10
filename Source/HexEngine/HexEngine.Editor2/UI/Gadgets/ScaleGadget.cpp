
#include "ScaleGadget.hpp"
#include "../EditorUI.hpp"

namespace HexEditor
{
	ScaleGadget::ScaleGadget() : 
		Gadget({ 'S' }, VK_LBUTTON, VK_RBUTTON)
	{}

	bool ScaleGadget::StartGadget()
	{
		int32_t mx, my;
		g_pEnv->_inputSystem->GetMousePosition(mx, my);

		auto inspector = g_pUIManager->GetInspector();
		auto canvas = g_pUIManager->GetSceneView();
		auto ent = inspector->GetInspectingEntity();

		if (!ent)
			return false;

		int32_t scrx, scry;
		g_pEnv->_inputSystem->GetWorldToScreenPosition(
			g_pEnv->_sceneManager->GetCurrentScene()->GetMainCamera(),
			ent->GetPosition(),
			scrx, scry,
			canvas->GetSize().x, canvas->GetSize().y);

		{
			_originalScale = inspector->GetInspectingEntity()->GetScale();

			_adjustStartX = scrx;
			_adjustStartY = scry;

			int32_t dx = (mx - _adjustStartX);
			int32_t dy = (my - _adjustStartY);

			_adjustSize = sqrt((dx * dx) + (dy * dy));
		}

		_scaleFreedom = math::Vector3(1.0f, 1.0f, 1.0f);

		return true;
	}

	void ScaleGadget::Update()
	{
		int32_t mx, my;
		g_pEnv->_inputSystem->GetMousePosition(mx, my);

		auto inspector = g_pUIManager->GetInspector();
		auto ent = inspector->GetInspectingEntity();

		int32_t dx = (mx - _adjustStartX);
		int32_t dy = (my - _adjustStartY);

		float len = sqrt((dx * dx) + (dy * dy));
		len -= _adjustSize;

		len /= 100.0f;

		ent->SetScale(_originalScale + (math::Vector3(len) * _scaleFreedom));
	}

	void ScaleGadget::StopGadget(GadgetAction action)
	{
		auto inspector = g_pUIManager->GetInspector();
		auto ent = inspector->GetInspectingEntity();

		if (action == GadgetAction::Confirm)
		{

		}
		else if (action == GadgetAction::Cancel)
		{
			ent->SetScale(_originalScale);
		}
	}

	bool ScaleGadget::OnInputEvent(InputEvent event, InputData* data)
	{
		bool ret = Gadget::OnInputEvent(event, data);

		if (_gadgetStarted && event == InputEvent::KeyDown)
		{
			auto inspector = g_pUIManager->GetInspector();
			auto ent = inspector->GetInspectingEntity();

			switch (data->KeyDown.key)
			{
			case 'X':
				ent->SetScale(_originalScale);
				_scaleFreedom = math::Vector3(1.0f, 0.0f, 0.0f);
				break;

			case 'Y':
				ent->SetScale(_originalScale);
				_scaleFreedom = math::Vector3(0.0f, 1.0f, 0.0f);
				break;

			case 'Z':
				ent->SetScale(_originalScale);
				_scaleFreedom = math::Vector3(0.0f, 0.0f, 1.0f);
				break;
			}
		}

		return ret;
	}
}