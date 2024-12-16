
#include "EntityList.hpp"
#include "../EditorUI.hpp"
#include "../../Editor.hpp"
#include <HexEngine.Core\FileSystem\ResourceSystem.hpp>
#include <HexEngine.Core\Entity\Entity.hpp>
#include <HexEngine.Core\Environment\IEnvironment.hpp>
#include <HexEngine.Core\Scene\SceneManager.hpp>

namespace HexEditor
{
	EntityList::EntityList(Element* parent, const HexEngine::Point& position, const HexEngine::Point& size) :
		HexEngine::EntityList(parent, position, size)
	{
		_onEntityClicked = std::bind(&Inspector::InspectEntity, g_pUIManager->GetInspector(), std::placeholders::_2);
		_onEntityParented = std::bind(&EntityList::SetEntityParent, this, std::placeholders::_2, std::placeholders::_3);
	}

	EntityList::~EntityList()
	{
		
	}

	void EntityList::SetEntityParent(HexEngine::Entity* sourceEnt, HexEngine::Entity* targetEnt)
	{
		if (sourceEnt == targetEnt)
			return;

		if (sourceEnt && targetEnt)
		{
			sourceEnt->SetParent(targetEnt);
		}

	}

	void EntityList::SaveAsPrefab(Entity* entity, FileSystem* fs)
	{
		HexEngine::EntityList::SaveAsPrefab(entity, g_pEditor->_projectFS);
	}

	void EntityList::Render(GuiRenderer* renderer, uint32_t w, uint32_t h)
	{
		//auto dl = renderer->PushDrawList();

		HexEngine::EntityList::Render(renderer, w, h);

		//renderer->ListDraw(dl);
		//renderer->PopDrawList();
	}

	/*void EntityList::DuplicateEntity(HexEngine::Entity* entity)
	{
		g_pEnv->_sceneManager->GetCurrentScene()->CloneEntity(entity, entity->GetName(), entity->GetPosition(), entity->GetRotation(), entity->GetScale());
		_ctx->DeleteMe();
		_ctx = nullptr;
	}*/
}