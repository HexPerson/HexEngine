

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

				// preserveWorldPosition=false: the clone was just created at
				// root with its local matching sourceChild's local relative
				// to sourceParent. We want that local kept verbatim under
				// clonedParent so the cloned hierarchy mirrors the source's
				// shape exactly (same offsets, same world layout). Default
				// true would re-derive local from a bogus root-space "world"
				// and shift the child away from its sibling-correct slot.
				clonedChild->SetParent(clonedParent, false);
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
			// Anchor in WORLD space so the drag delta - which is a camera
			// right/up vector in world space - composes correctly even when
			// the duplicated entity is parented under something rotated or
			// scaled. Update() converts back to local before writing.
			_originalWorldPosition = copyEnt->GetWorldTM().Translation();

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

		// Compose the drag in world space against the world-space anchor
		// captured in StartGadget.
		math::Vector3 newWorldPos = _originalWorldPosition;
		newWorldPos += rightVec * (float)dx;
		newWorldPos -= upVec * (float)dy;

		// ForcePosition writes the LOCAL position, so convert back through
		// the parent's inverse world transform when the duplicate is parented.
		// Without this, dragging an entity whose parent has non-identity
		// rotation/scale would move it along the parent's local axes instead
		// of the camera plane the user is dragging on - and any non-zero
		// parent translation would compound into the wrong final world
		// position. Unparented entities have world == local, so the inverse
		// step is a no-op there.
		math::Vector3 newLocalPos = newWorldPos;
		if (auto* parent = _duplicatedEntity->GetParent(); parent != nullptr)
		{
			const math::Matrix& parentWorldInv = parent->GetWorldTMInvert();
			newLocalPos = math::Vector3::Transform(newWorldPos, parentWorldInv);
		}

		_duplicatedEntity->ForcePosition(newLocalPos);
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
				message.sourceId = sourceEntity != nullptr ? sourceEntity->GetId() : HexEngine::InvalidEntityId;
				message.duplicateId = duplicatedEntity != nullptr ? duplicatedEntity->GetId() : HexEngine::InvalidEntityId;
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
