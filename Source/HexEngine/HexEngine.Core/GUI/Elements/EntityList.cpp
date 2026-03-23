
#include "EntityList.hpp"
#include "../../FileSystem/FileSystem.hpp"
#include "../../Entity\Entity.hpp"
#include "../../Environment\IEnvironment.hpp"
#include "../../Scene\SceneManager.hpp"

namespace HexEngine
{
	EntityList::EntityList(Element* parent, const HexEngine::Point& position, const HexEngine::Point& size) :
		TreeList(parent, position, size)
	{
		CreateIcons();

		//_onSelect = std::bind(&EntityList::OnClickEntityInList, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
		//_onDragAndDrop = std::bind(&EntityList::OnDragAndDropEntity, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
	}

	EntityList::~EntityList()
	{
	}

	bool EntityList::OnInputEvent(InputEvent event, InputData* data)
	{
		if (_ctx && _ctx->IsEnabled())
			return false;

		return TreeList::OnInputEvent(event, data);
	}

	void EntityList::OnClickEntityInList(ListNode* item, int32_t mouseButton)
	{
		if (mouseButton == VK_LBUTTON)
		{
			auto scene = HexEngine::g_pEnv->_sceneManager->GetCurrentScene();

			if (!scene)
				return;

			std::string entName(item->GetLabel().begin(), item->GetLabel().end());

			auto entity = scene->GetEntityByName(entName);

			if (entity == nullptr)
				return;
			
			if (_onEntityClicked)
				_onEntityClicked(this, entity);

		}
		else if (mouseButton == VK_RBUTTON)
		{
			auto scene = HexEngine::g_pEnv->_sceneManager->GetCurrentScene();

			if (!scene)
				return;

			std::string entName(item->GetLabel().begin(), item->GetLabel().end());

			auto entity = scene->GetEntityByName(entName);

			if (entity == nullptr)
			{
				if (dynamic_cast<SceneListNode*>(item) != nullptr)
				{
					int32_t mx, my;
					g_pEnv->_inputSystem->GetMousePosition(mx, my);

					Point p(mx, my);

					_ctx = new ContextMenu(this, p.RelativeTo(GetAbsolutePosition()));

					_ctx->AddItem(new ContextItem(L"Load Scene", std::bind(&EntityList::OnLoadScene, this, (SceneListNode*)item)));
					return;
				}
			}
			else
			{
				int32_t mx, my;
				g_pEnv->_inputSystem->GetMousePosition(mx, my);

				Point p(mx, my);

				_ctx = new ContextMenu(this, p.RelativeTo(GetAbsolutePosition()));

				_ctx->AddItem(new ContextItem(L"Duplicate", std::bind(&EntityList::DuplicateEntity, this, entity)));
				_ctx->AddItem(new ContextItem(L"Save as prefab", std::bind(&EntityList::SaveAsPrefab, this, entity, &g_pEnv->GetFileSystem())));
			}
		}
	}

	void EntityList::OnLoadScene(SceneListNode* node)
	{
		auto currentScene = g_pEnv->_sceneManager->GetCurrentScene();

		currentScene->SetFlags(SceneFlags::Disabled);

		node->GetScene()->SetFlags(SceneFlags::Renderable | SceneFlags::Updateable | SceneFlags::PostProcessingEnabled);
	}

	bool EntityList::OnDragAndDropEntity(TreeList* list, ListNode* dragSource, ListNode* dragTarget)
	{
		(void)list;

		if (dragSource == nullptr || dragTarget == nullptr)
			return false;

		auto scene = g_pEnv->_sceneManager->GetCurrentScene();
		if (scene == nullptr)
			return false;

		auto sourceEnt = scene->GetEntityByName(std::string(dragSource->GetLabel().begin(), dragSource->GetLabel().end()));
		if (sourceEnt == nullptr)
			return false;

		Entity* targetEnt = nullptr;
		if (dynamic_cast<SceneListNode*>(dragTarget) == nullptr)
		{
			targetEnt = scene->GetEntityByName(std::string(dragTarget->GetLabel().begin(), dragTarget->GetLabel().end()));
			if (targetEnt == nullptr)
				return false;
		}

		// This will cause a black hole to form and collapse in on itself, best not to play with the universe like this
		if (sourceEnt == targetEnt)
			return true;

		if (sourceEnt && targetEnt)
		{
			if (_onEntityParented)
				_onEntityParented(this, sourceEnt, targetEnt);

			//sourceEnt->SetParent(targetEnt);
		}

		RefreshList();

		return true;
	}

	void EntityList::RefreshList()
	{
		const auto& scenes = g_pEnv->_sceneManager->GetAllScenes();
		Clear();

		for (auto& scene : scenes)
		{
			if (HEX_HASFLAG(scene->GetFlags(), SceneFlags::Utility))
				continue;

			auto sceneRoot = AddScene(scene);

			auto allEnts = scene->GetEntities();

			for (auto& entSet : allEnts)
			{
				for (auto& ent : entSet.second)
				{
					AddEntity(ent, sceneRoot);
				}
			}
		}
	}

	void EntityList::DuplicateEntity(Entity* entity)
	{
		auto* duplicate = g_pEnv->_sceneManager->GetCurrentScene()->CloneEntity(entity, entity->GetName(), entity->GetPosition(), entity->GetRotation(), entity->GetScale());
		if (_onEntityDuplicated && duplicate != nullptr)
		{
			_onEntityDuplicated(this, entity, duplicate);
		}

		_ctx->DeleteMe();
		_ctx = nullptr;
	}

	void EntityList::SaveAsPrefab(Entity* entity, FileSystem* fs)
	{
		auto sceneManager = g_pEnv->_sceneManager;
		auto currentScene = sceneManager->GetCurrentScene();
		auto prefabScene = sceneManager->CreateEmptyScene(false);

		sceneManager->SetActiveScene(prefabScene);
		auto rootEntity = prefabScene->CloneEntity(entity);

		for (auto children : entity->GetChildren())
		{
			auto newChild = prefabScene->CloneEntity(children, false);

			newChild->SetParent(rootEntity);
		}

		SceneSaveFile saveFile(
			fs->GetLocalAbsoluteDataPath("Prefabs/" + entity->GetName() + ".hprefab"),
			std::ios::out,
			prefabScene,
			SceneFileFlags::DontSaveVariables);

		saveFile.Save();
		
		sceneManager->SetActiveScene(currentScene);
	}

	void EntityList::CreateIcons()
	{
		_icons[IconId::Entity] = HexEngine::ITexture2D::Create("EngineData.Textures/UI/entity.png");
		_icons[IconId::Folder] = HexEngine::ITexture2D::Create("EngineData.Textures/UI/folder.png");
		_icons[IconId::Scene] = HexEngine::ITexture2D::Create("EngineData.Textures/UI/scene.png");
	}

	ListNode* EntityList::AddScene(const std::shared_ptr<Scene>& scene)
	{
		scene->Lock();

		auto sceneNode = new SceneListNode(this, scene->GetName(), { _icons[IconId::Scene].get(), _icons[IconId::Scene].get() }, scene.get());

		if(_onSceneClicked)
			sceneNode->_onClick = std::bind(_onSceneClicked, this, sceneNode->GetScene());

		AddNode(sceneNode, nullptr, false);

		scene->Unlock();

		return sceneNode;
	}

	void EntityList::AddEntity(HexEngine::Entity* entity, ListNode* scene)
	{
		if (entity == nullptr || scene == nullptr)
			return;

		// Avoid duplicate rows when adding parent chains.
		if (FindItemByObjectPtr(entity, scene) != nullptr)
			return;

		std::wstring entNameStr = std::wstring(entity->GetName().begin(), entity->GetName().end());

		if (auto parent = entity->GetParent(); parent != nullptr)
		{
			AddEntity(parent, scene);

			auto parentItem = FindItemByObjectPtr(parent, scene);

			if (parentItem)
			{
				auto node = new ListNode(this, entNameStr, { _icons[IconId::Entity].get() }, entity);

				node->_onClick = std::bind(&EntityList::OnClickEntityInList, this, std::placeholders::_1, std::placeholders::_2);
				node->_onDragAndDrop = std::bind(&EntityList::OnDragAndDropEntity, this, this, std::placeholders::_1, std::placeholders::_2);

				AddNode(node, parentItem, false);				

				return;
			}
		}

		auto node = new ListNode(this, entNameStr, { _icons[IconId::Entity].get() }, entity);
		node->_onClick = std::bind(&EntityList::OnClickEntityInList, this, std::placeholders::_1, std::placeholders::_2);
		node->_onDragAndDrop = std::bind(&EntityList::OnDragAndDropEntity, this, this, std::placeholders::_1, std::placeholders::_2);

		AddNode(node, scene, false);

		//entity->GetScene()->Unlock();
	}

	void EntityList::AddEntity(HexEngine::Entity* entity)
	{
		auto sceneParentItem = FindItemByObjectPtr(entity->GetScene());

		AddEntity(entity, sceneParentItem);
	}

	void EntityList::RemoveEntity(HexEngine::Entity* entity)
	{
		RefreshList();

		//auto& name = entity->GetName();
		//RemoveItem(std::wstring(name.begin(), name.end()));
	}

	void EntityList::Render(GuiRenderer* renderer, uint32_t w, uint32_t h)
	{
		TreeList::Render(renderer, w, h);
	}
}
