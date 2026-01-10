

#include "DuplicateGadget.hpp"
#include "../EditorUI.hpp"

namespace HexEditor
{
	DuplicateGadget::DuplicateGadget() :
		Gadget({ VK_CONTROL, 'D' }, VK_LBUTTON, VK_RBUTTON)
	{}

	bool DuplicateGadget::StartGadget()
	{
		int32_t mx, my;
		g_pEnv->_inputSystem->GetMousePosition(mx, my);

		auto inspector = g_pUIManager->GetInspector();
		auto canvas = g_pUIManager->GetSceneView();
		auto ent = inspector->GetInspectingEntity();

		if (!ent)
			return false;

		auto copyEnt = g_pEnv->_sceneManager->GetCurrentScene()->CloneEntity(ent);

		inspector->InspectEntity(copyEnt);

		int32_t scrx, scry;
		if (g_pEnv->_inputSystem->GetWorldToScreenPosition(
			g_pEnv->_sceneManager->GetCurrentScene()->GetMainCamera(),
			copyEnt->GetWorldTM().Translation(),
			scrx, scry,
			canvas->GetSize().x, canvas->GetSize().y))
		{
			_originalPosition = copyEnt->GetPosition();

			_adjustStartX = mx;
			_adjustStartY = my;

			_cameraRotation = g_pEnv->_sceneManager->GetCurrentScene()->GetMainCamera()->GetEntity()->GetRotation();
		}

		return true;
	}

	void DuplicateGadget::Update()
	{
		int32_t mx, my;
		g_pEnv->_inputSystem->GetMousePosition(mx, my);

		auto inspector = g_pUIManager->GetInspector();
		auto ent = inspector->GetInspectingEntity();

		int32_t dx = (mx - _adjustStartX);
		int32_t dy = (my - _adjustStartY);

		math::Vector3 rightVec = math::Vector3::Transform(math::Vector3::Right, _cameraRotation);
		math::Vector3 upVec = math::Vector3::Transform(math::Vector3::Up, _cameraRotation);
		math::Vector3 forwardVec = math::Vector3::Transform(math::Vector3::Forward, _cameraRotation);

		math::Vector3 newPos = _originalPosition;
		newPos += rightVec * (float)dx;
		newPos -= upVec * (float)dy;

		ent->ForcePosition(newPos);
	}

	void DuplicateGadget::StopGadget(GadgetAction action)
	{
		auto inspector = g_pUIManager->GetInspector();
		auto ent = inspector->GetInspectingEntity();

		if (action == GadgetAction::Confirm)
		{

		}
		else if (action == GadgetAction::Cancel)
		{
			ent->SetPosition(_originalPosition);
		}
	}
}