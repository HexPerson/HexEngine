
#include "SceneView.hpp"
#include "../EditorUI.hpp"

namespace HexEditor
{
	SceneView::SceneView(Element* parent, const HexEngine::Point& position, const HexEngine::Point& size) :
		Element(parent, position, size)
	{
	}

	bool SceneView::OnInputEvent(HexEngine::InputEvent event, HexEngine::InputData* data)
	{
		if (event == HexEngine::InputEvent::MouseDown && IsMouseOver(true))
		{
			_mouseActionStartPos.x = data->MouseDown.xpos;
			_mouseActionStartPos.y = data->MouseDown.ypos;

			switch (data->MouseDown.button)
			{
			case VK_RBUTTON:
				_roamState = RoamState::FreeLook;
				break;
			}
			return false;
		}
		else if (event == HexEngine::InputEvent::MouseUp)
		{
			_roamState = RoamState::None;

			if (data->MouseUp.button == VK_LBUTTON && _dragAndDropEntity != nullptr)
			{
				g_pUIManager->GetInspector()->InspectEntity(_dragAndDropEntity);
				_dragAndDropEntity = nullptr;
			}
			return true;
		}

		// handle resource drag & dropping

		if (event == HexEngine::InputEvent::MouseMove && IsMouseOver(true))
		{
			if (auto draggingAsset = g_pUIManager->GetExplorer()->GetCurrentlyDraggedAsset(); draggingAsset != nullptr && draggingAsset->path.extension() == ".hmesh")
			{
				auto hit = g_pUIManager->RayCastWorld({ _dragAndDropEntity });

				if (hit.entity != nullptr)
				{
					if (_dragAndDropEntity == nullptr)
					{
						_dragAndDropEntity = HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->CreateEntity(
							ws2s(draggingAsset->assetNameShort),
							hit.position,
							math::Quaternion(),
							math::Vector3(10.0f)
						);

						auto staticMesh = _dragAndDropEntity->AddComponent<HexEngine::StaticMeshComponent>();

						staticMesh->SetMesh(HexEngine::Mesh::Create(draggingAsset->path));
					}
					else
					{
						_dragAndDropEntity->SetPosition(hit.position);
					}

				}
				
			}
		}

		return Element::OnInputEvent(event, data);
	}

	void SceneView::Render(HexEngine::GuiRenderer* renderer, uint32_t w, uint32_t h)
	{
		if (auto scene = HexEngine::g_pEnv->_sceneManager->GetCurrentScene(); scene != nullptr)
		{
			if (auto mainCamera = scene->GetMainCamera(); mainCamera != nullptr)
			{
				renderer->FillTexturedQuad(mainCamera->GetRenderTarget(),
					GetPosition().x, GetPosition().y,
					GetSize().x, GetSize().y,
					math::Color(1, 1, 1, 1));
			}
		}
	}
}