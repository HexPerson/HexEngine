
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
		auto canvas = g_pUIManager->GetCanvas();
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

		ent->SetScale(_originalScale + math::Vector3(len));
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
}