

#include "DuplicateGadget.hpp"
#include "../EditorUI.hpp"
#include <vector>

namespace HexEditor
{
	namespace
	{
		bool CloneEntityChildrenRecursive(HexEngine::Scene* scene, HexEngine::Entity* sourceParent, HexEngine::Entity* clonedParent)
		{
			if (scene == nullptr || sourceParent == nullptr || clonedParent == nullptr)
				return false;

			const auto sourceChildren = sourceParent->GetChildren();
			for (auto* sourceChild : sourceChildren)
			{
				if (sourceChild == nullptr || sourceChild->IsPendingDeletion())
					continue;

				auto* clonedChild = scene->CloneEntity(sourceChild, false);
				if (clonedChild == nullptr)
					return false;

				clonedChild->SetParent(clonedParent);
				if (!CloneEntityChildrenRecursive(scene, sourceChild, clonedChild))
					return false;
			}

			return true;
		}

		HexEngine::Entity* CloneEntityHierarchy(HexEngine::Scene* scene, HexEngine::Entity* sourceRoot)
		{
			if (scene == nullptr || sourceRoot == nullptr || sourceRoot->IsPendingDeletion())
				return nullptr;

			auto* clonedRoot = scene->CloneEntity(sourceRoot);
			if (clonedRoot == nullptr)
				return nullptr;

			if (!CloneEntityChildrenRecursive(scene, sourceRoot, clonedRoot))
			{
				scene->DestroyEntity(clonedRoot);
				return nullptr;
			}

			return clonedRoot;
		}
	}

	DuplicateGadget::DuplicateGadget() :
		Gadget({ VK_CONTROL, 'D' }, VK_LBUTTON, VK_RBUTTON)
	{}

	bool DuplicateGadget::StartGadget()
	{
		_sourceEntity = nullptr;
		_duplicatedEntity = nullptr;

		int32_t mx, my;
		HexEngine::g_pEnv->_inputSystem->GetMousePosition(mx, my);

		auto inspector = g_pUIManager->GetInspector();
		auto canvas = g_pUIManager->GetSceneView();
		auto ent = inspector->GetInspectingEntity();
		const auto viewportSize = canvas->GetSceneViewportSize();

		if (!ent)
			return false;

		_sourceEntity = ent;
		auto* currentScene = HexEngine::g_pEnv->_sceneManager->GetCurrentScene().get();
		auto copyEnt = CloneEntityHierarchy(currentScene, ent);
		if (copyEnt == nullptr)
			return false;

		_duplicatedEntity = copyEnt;

		inspector->InspectEntity(copyEnt);

		StopGadget(GadgetAction::Confirm);



		int32_t scrx, scry;
		if (HexEngine::g_pEnv->_inputSystem->GetWorldToScreenPosition(
			HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->GetMainCamera(),
			copyEnt->GetWorldTM().Translation(),
			scrx, scry,
			viewportSize.x, viewportSize.y))
		{
			_originalPosition = copyEnt->GetPosition();

			_adjustStartX = mx;
			_adjustStartY = my;

			_cameraRotation = HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->GetMainCamera()->GetEntity()->GetRotation();
		}

		return true;
	}

	void DuplicateGadget::Update()
	{
		if (_duplicatedEntity == nullptr)
			return;

		int32_t mx, my;
		HexEngine::g_pEnv->_inputSystem->GetMousePosition(mx, my);

		int32_t dx = (mx - _adjustStartX);
		int32_t dy = (my - _adjustStartY);

		math::Vector3 rightVec = math::Vector3::Transform(math::Vector3::Right, _cameraRotation);
		math::Vector3 upVec = math::Vector3::Transform(math::Vector3::Up, _cameraRotation);
		math::Vector3 forwardVec = math::Vector3::Transform(math::Vector3::Forward, _cameraRotation);

		math::Vector3 newPos = _originalPosition;
		newPos += rightVec * (float)dx;
		newPos -= upVec * (float)dy;

		_duplicatedEntity->ForcePosition(newPos);
	}

	void DuplicateGadget::StopGadget(GadgetAction action)
	{
		auto inspector = g_pUIManager->GetInspector();
		auto duplicatedEntity = _duplicatedEntity;
		auto sourceEntity = _sourceEntity;

		_duplicatedEntity = nullptr;

		if (duplicatedEntity == nullptr)
			return;

		if (action == GadgetAction::Confirm)
		{
			if (g_pUIManager != nullptr)
			{
				HexEngine::EditorEntityDuplicatedMessage message(sourceEntity, duplicatedEntity);
				g_pUIManager->BroadcastEditorToolMessage(message);
			}

			g_pUIManager->RecordEntityCreated(duplicatedEntity);
		}
		else if (action == GadgetAction::Cancel)
		{
			auto currentScene = HexEngine::g_pEnv->_sceneManager->GetCurrentScene();
			if (currentScene != nullptr)
			{
				currentScene->DestroyEntity(duplicatedEntity);
			}

			inspector->InspectEntity(sourceEntity);
		}

		_sourceEntity = nullptr;
	}
}
