
#include "PositionGadget.hpp"
#include "../EditorUI.hpp"

namespace HexEditor
{
	PositionGadget::PositionGadget() :
		Gadget({ 'G' }, VK_LBUTTON, VK_RBUTTON)
	{}

	bool PositionGadget::StartGadget()
	{
		// we want to allow 3 degrees of movement as standard, this can be changed by pressing x, y, or z later
		_movementFreedom = math::Vector3(1.0f);

		int32_t mx, my;
		g_pEnv->_inputSystem->GetMousePosition(mx, my);

		auto inspector = g_pUIManager->GetInspector();
		auto canvas = g_pUIManager->GetCanvas();
		auto ent = inspector->GetInspectingEntity();

		if (!ent)
			return false;

		int32_t scrx, scry;
		if (g_pEnv->_inputSystem->GetWorldToScreenPosition(
			g_pEnv->_sceneManager->GetCurrentScene()->GetMainCamera(),
			ent->GetWorldTM().Translation(),
			scrx, scry,
			canvas->GetSize().x, canvas->GetSize().y))
		{
			_originalPosition = ent->GetPosition();

			_adjustStartX = mx;
			_adjustStartY = my;

			_cameraRotation = g_pEnv->_sceneManager->GetCurrentScene()->GetMainCamera()->GetEntity()->GetRotation();
		}

		return true;
	}

	bool PositionGadget::OnInputEvent(InputEvent event, InputData* data)
	{
		bool ret = Gadget::OnInputEvent(event, data);

		if (_gadgetStarted && event == InputEvent::KeyDown)
		{
			auto inspector = g_pUIManager->GetInspector();
			auto ent = inspector->GetInspectingEntity();

			switch (data->KeyDown.key)
			{
			case 'X':
				ent->SetPosition(_originalPosition);
				_movementFreedom = math::Vector3(1.0f, 0.0f, 0.0f);
				break;

			case 'Y':
				ent->SetPosition(_originalPosition);
				_movementFreedom = math::Vector3(0.0f, 1.0f, 0.0f);
				break;

			case 'Z':
				ent->SetPosition(_originalPosition);
				_movementFreedom = math::Vector3(0.0f, 0.0f, 1.0f);
				break;
			}
		}

		return ret;
	}

	void PositionGadget::Update()
	{
		int32_t mx, my;
		g_pEnv->_inputSystem->GetMousePosition(mx, my);

		auto inspector = g_pUIManager->GetInspector();
		auto ent = inspector->GetInspectingEntity();

		const float inputSensitivity = 0.7f;

		float dx = ((float)mx - (float)_adjustStartX) * inputSensitivity;
		float dy = ((float)my - (float)_adjustStartY) * inputSensitivity;

		math::Vector3 rightVec = math::Vector3::Transform(math::Vector3::Right, _cameraRotation);
		math::Vector3 upVec = math::Vector3::Transform(math::Vector3::Up, _cameraRotation);
		math::Vector3 forwardVec = math::Vector3::Transform(math::Vector3::Forward, _cameraRotation);

		math::Vector3 newPos = _originalPosition;
		newPos += (rightVec * (float)dx) * _movementFreedom;
		newPos -= (upVec * (float)dy) * _movementFreedom;

		ent->ForcePosition(newPos);
	}

	void PositionGadget::StopGadget(GadgetAction action)
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