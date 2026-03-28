
#include "EntityList.hpp"
#include "../../FileSystem/FileSystem.hpp"
#include "../../Entity\Entity.hpp"
#include "../../Environment\IEnvironment.hpp"
#include "../../Scene\SceneManager.hpp"
#include <algorithm>
#include <cwctype>
#include <unordered_map>

namespace HexEngine
{
	namespace
	{
		struct EntityListState
		{
			std::unordered_map<const Entity*, bool> entityOpenState;
			std::unordered_map<const Scene*, bool> sceneOpenState;
			std::wstring filterText;
			std::vector<std::wstring> filterTokens;
			Entity* selectedEntity = nullptr;
		};

		std::unordered_map<const EntityList*, EntityListState> g_entityListStates;

		EntityListState& GetState(const EntityList* list)
		{
			return g_entityListStates[list];
		}

		const EntityListState& GetStateConst(const EntityList* list)
		{
			auto it = g_entityListStates.find(list);
			if (it != g_entityListStates.end())
				return it->second;

			static const EntityListState emptyState{};
			return emptyState;
		}
	}

	EntityList::EntityList(Element* parent, const HexEngine::Point& position, const HexEngine::Point& size) :
		TreeList(parent, position, size)
	{
		(void)GetState(this);
		CreateIcons();

		//_onSelect = std::bind(&EntityList::OnClickEntityInList, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
		//_onDragAndDrop = std::bind(&EntityList::OnDragAndDropEntity, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
	}

	EntityList::~EntityList()
	{
		g_entityListStates.erase(this);
	}

	bool EntityList::OnInputEvent(InputEvent event, InputData* data)
	{
		if (_ctx && _ctx->IsEnabled())
			return false;

		return TreeList::OnInputEvent(event, data);
	}

	void EntityList::OnClickEntityInList(ListNode* item, int32_t mouseButton)
	{
		if (item == nullptr)
			return;

		if (mouseButton == VK_LBUTTON)
		{
			auto* entity = ResolveEntityNode(item);
			if (entity == nullptr || entity->IsPendingDeletion())
				return;

			auto& state = GetState(this);
			state.selectedEntity = entity;
			FocusEntity(entity);

			if (_onEntityClicked)
				_onEntityClicked(this, entity);
			return;
		}

		if (mouseButton == VK_RBUTTON)
		{
			if (auto* sceneNode = dynamic_cast<SceneListNode*>(item); sceneNode != nullptr)
			{
				int32_t mx, my;
				g_pEnv->_inputSystem->GetMousePosition(mx, my);

				Point p(mx, my);

				_ctx = new ContextMenu(this, p.RelativeTo(GetAbsolutePosition()));

				_ctx->AddItem(new ContextItem(L"Load Scene", std::bind(&EntityList::OnLoadScene, this, sceneNode)));
				return;
			}

			auto* entity = ResolveEntityNode(item);
			if (entity == nullptr || entity->IsPendingDeletion())
				return;

			int32_t mx, my;
			g_pEnv->_inputSystem->GetMousePosition(mx, my);

			Point p(mx, my);

			_ctx = new ContextMenu(this, p.RelativeTo(GetAbsolutePosition()));

			_ctx->AddItem(new ContextItem(L"Duplicate", std::bind(&EntityList::DuplicateEntity, this, entity)));
			_ctx->AddItem(new ContextItem(L"Save as prefab", std::bind(&EntityList::SaveAsPrefab, this, entity, &g_pEnv->GetFileSystem())));
		}
	}

	Entity* EntityList::ResolveEntityNode(const ListNode* node) const
	{
		if (node == nullptr)
			return nullptr;

		if (dynamic_cast<const SceneListNode*>(node) != nullptr)
			return nullptr;

		return node->GetObjectAs<Entity>();
	}

	Scene* EntityList::ResolveSceneNode(const ListNode* node) const
	{
		if (auto* sceneNode = dynamic_cast<const SceneListNode*>(node); sceneNode != nullptr)
		{
			return sceneNode->GetScene();
		}

		if (auto* entity = ResolveEntityNode(node); entity != nullptr)
		{
			return entity->GetScene();
		}

		return nullptr;
	}

	bool EntityList::IsAncestorOf(const Entity* source, const Entity* potentialChild) const
	{
		if (source == nullptr || potentialChild == nullptr)
			return false;

		for (auto* ancestor = potentialChild->GetParent(); ancestor != nullptr; ancestor = ancestor->GetParent())
		{
			if (ancestor == source)
				return true;
		}

		return false;
	}

	ListNode* EntityList::AddEntityInternal(Entity* entity, ListNode* sceneNode, std::unordered_set<Entity*>& parentWalkGuard)
	{
		if (entity == nullptr || sceneNode == nullptr || entity->IsPendingDeletion())
			return nullptr;

		Scene* scene = ResolveSceneNode(sceneNode);
		if (scene == nullptr || entity->GetScene() != scene)
			return nullptr;

		if (!parentWalkGuard.insert(entity).second)
			return FindItemByObjectPtr(entity, sceneNode);

		auto removeFromGuard = [&]()
		{
			parentWalkGuard.erase(entity);
		};

		if (auto* existingNode = FindItemByObjectPtr(entity, sceneNode); existingNode != nullptr)
		{
			removeFromGuard();
			return existingNode;
		}

		ListNode* parentNode = sceneNode;
		if (auto* parent = entity->GetParent(); parent != nullptr && parent->GetScene() == scene)
		{
			parentNode = AddEntityInternal(parent, sceneNode, parentWalkGuard);
			if (parentNode == nullptr)
				parentNode = sceneNode;
		}

		const std::wstring entName(entity->GetName().begin(), entity->GetName().end());
		auto* node = new ListNode(this, entName, { _icons[IconId::Entity].get() }, entity);
		node->_onClick = std::bind(&EntityList::OnClickEntityInList, this, std::placeholders::_1, std::placeholders::_2);
		node->_onDragAndDrop = std::bind(&EntityList::OnDragAndDropEntity, this, this, std::placeholders::_1, std::placeholders::_2);
		auto& listState = GetState(this);
		if (auto state = listState.entityOpenState.find(entity); state != listState.entityOpenState.end())
		{
			node->SetOpen(state->second);
		}
		else if (!listState.filterTokens.empty())
		{
			node->SetOpen(true);
		}

		AddNode(node, parentNode, false);

		removeFromGuard();
		return node;
	}

	void EntityList::OnLoadScene(SceneListNode* node)
	{
		auto currentScene = g_pEnv->_sceneManager->GetCurrentScene();
		if (currentScene == nullptr || node == nullptr)
			return;

		currentScene->SetFlags(SceneFlags::Disabled);
		node->GetScene()->SetFlags(SceneFlags::Renderable | SceneFlags::Updateable | SceneFlags::PostProcessingEnabled);
	}

	bool EntityList::OnDragAndDropEntity(TreeList* list, ListNode* dragSource, ListNode* dragTarget)
	{
		(void)list;

		if (dragSource == nullptr || dragTarget == nullptr)
			return false;

		auto* sourceEnt = ResolveEntityNode(dragSource);
		if (sourceEnt == nullptr || sourceEnt->IsPendingDeletion())
			return false;

		auto* targetEnt = ResolveEntityNode(dragTarget);
		auto* sourceScene = sourceEnt->GetScene();
		auto* targetScene = ResolveSceneNode(dragTarget);

		if (sourceScene == nullptr || targetScene == nullptr || sourceScene != targetScene)
			return false;

		if (sourceEnt == targetEnt)
			return false;

		if (targetEnt != nullptr && (targetEnt->IsPendingDeletion() || IsAncestorOf(sourceEnt, targetEnt)))
			return false;

		if (sourceEnt->GetParent() == targetEnt)
			return true;

		if (_onEntityParented)
			_onEntityParented(this, sourceEnt, targetEnt);

		RefreshList();
		return true;
	}

	void EntityList::RefreshList()
	{
		auto& state = GetState(this);
		CaptureTreeState();
		Clear();

		auto scene = g_pEnv->_sceneManager->GetCurrentScene();
		if (scene == nullptr)
			return;

		auto* sceneRoot = AddScene(scene);
		if (sceneRoot == nullptr)
			return;

		std::vector<Entity*> entities;
		std::unordered_set<Entity*> uniqueEntities;
		for (const auto& entSet : scene->GetEntities())
		{
			for (auto* ent : entSet.second)
			{
				if (ent != nullptr &&
					!ent->IsPendingDeletion() &&
					ent->GetScene() == scene.get() &&
					uniqueEntities.insert(ent).second)
				{
					entities.push_back(ent);
				}
			}
		}

		std::sort(entities.begin(), entities.end(),
			[](const Entity* lhs, const Entity* rhs)
			{
				return lhs->GetName() < rhs->GetName();
			});

		std::unordered_set<Entity*> filteredVisibilitySet;
		if (!state.filterTokens.empty())
		{
			for (auto* entity : entities)
			{
				if (!DoesEntityMatchFilter(entity))
					continue;

				for (auto* chain = entity; chain != nullptr && chain->GetScene() == scene.get(); chain = chain->GetParent())
				{
					filteredVisibilitySet.insert(chain);
				}
			}
		}

		for (auto* entity : entities)
		{
			if (!state.filterTokens.empty() && filteredVisibilitySet.find(entity) == filteredVisibilitySet.end())
				continue;

			std::unordered_set<Entity*> parentWalkGuard;
			AddEntityInternal(entity, sceneRoot, parentWalkGuard);
		}

		if (state.selectedEntity != nullptr && state.selectedEntity->GetScene() == scene.get() && !state.selectedEntity->IsPendingDeletion())
		{
			FocusEntity(state.selectedEntity);
		}
	}

	void EntityList::SetFilterText(const std::wstring& filterText)
	{
		auto& state = GetState(this);
		if (state.filterText == filterText)
			return;

		state.filterText = filterText;
		RebuildFilterTokens();
		RefreshList();
	}

	const std::wstring& EntityList::GetFilterText() const
	{
		return GetStateConst(this).filterText;
	}

	bool EntityList::FocusEntity(Entity* entity)
	{
		if (entity == nullptr || entity->IsPendingDeletion())
			return false;

		auto& state = GetState(this);
		state.selectedEntity = entity;

		auto* node = FindItemByObjectPtr(entity);
		if (node == nullptr)
			return false;

		for (auto* parent = node->GetParent(); parent != nullptr; parent = parent->GetParent())
		{
			parent->SetOpen(true);

			if (auto* parentEntity = ResolveEntityNode(parent); parentEntity != nullptr)
			{
				state.entityOpenState[parentEntity] = true;
			}
			else if (auto* parentScene = ResolveSceneNode(parent); parentScene != nullptr)
			{
				state.sceneOpenState[parentScene] = true;
			}
		}

		state.entityOpenState[entity] = node->IsOpen();
		SetSelectedItem(node, false);
		ScrollToItem(node, 28);
		Repaint();
		return true;
	}

	void EntityList::DuplicateEntity(Entity* entity)
	{
		if (entity == nullptr)
			return;

		auto* duplicate = g_pEnv->_sceneManager->GetCurrentScene()->CloneEntity(entity, entity->GetName(), entity->GetPosition(), entity->GetRotation(), entity->GetScale());
		if (_onEntityDuplicated && duplicate != nullptr)
		{
			_onEntityDuplicated(this, entity, duplicate);
		}

		if (_ctx != nullptr)
		{
			_ctx->DeleteMe();
			_ctx = nullptr;
		}
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
		if (scene == nullptr)
			return nullptr;

		auto sceneNode = new SceneListNode(this, scene->GetName(), { _icons[IconId::Scene].get(), _icons[IconId::Scene].get() }, scene.get());
		auto& listState = GetState(this);
		if (auto state = listState.sceneOpenState.find(scene.get()); state != listState.sceneOpenState.end())
		{
			sceneNode->SetOpen(state->second);
		}
		else
		{
			sceneNode->SetOpen(true);
		}

		if (_onSceneClicked)
		{
			sceneNode->_onClick = [this, scenePtr = sceneNode->GetScene()](ListNode*, int32_t mouseButton)
			{
				if (mouseButton == VK_LBUTTON && _onSceneClicked)
					_onSceneClicked(this, scenePtr);
			};
		}

		AddNode(sceneNode, nullptr, false);

		return sceneNode;
	}

	void EntityList::AddEntity(HexEngine::Entity* entity, ListNode* scene)
	{
		(void)entity;
		(void)scene;
		RefreshList();
	}

	void EntityList::AddEntity(HexEngine::Entity* entity)
	{
		if (entity == nullptr || entity->IsPendingDeletion())
			return;

		auto scene = g_pEnv->_sceneManager->GetCurrentScene();
		if (scene == nullptr || entity->GetScene() != scene.get())
			return;

		auto& state = GetState(this);
		state.selectedEntity = entity;
		RefreshList();
	}

	void EntityList::RemoveEntity(HexEngine::Entity* entity)
	{
		(void)entity;
		RefreshList();

		//auto& name = entity->GetName();
		//RemoveItem(std::wstring(name.begin(), name.end()));
	}

	void EntityList::Render(GuiRenderer* renderer, uint32_t w, uint32_t h)
	{
		TreeList::Render(renderer, w, h);
	}

	void EntityList::CaptureTreeState()
	{
		for (auto* root : GetRootItems())
		{
			CaptureNodeStateRecursive(root);
		}
	}

	void EntityList::CaptureNodeStateRecursive(const ListNode* node)
	{
		if (node == nullptr)
			return;

		if (auto* sceneNode = dynamic_cast<const SceneListNode*>(node); sceneNode != nullptr)
		{
			auto& state = GetState(this);
			state.sceneOpenState[sceneNode->GetScene()] = sceneNode->IsOpen();
		}
		else if (auto* entity = ResolveEntityNode(node); entity != nullptr)
		{
			auto& state = GetState(this);
			state.entityOpenState[entity] = node->IsOpen();
		}

		for (auto* child : node->GetChildren())
		{
			CaptureNodeStateRecursive(child);
		}
	}

	void EntityList::RebuildFilterTokens()
	{
		auto& state = GetState(this);
		state.filterTokens.clear();

		std::wstring currentToken;
		currentToken.reserve(state.filterText.size());

		for (wchar_t ch : state.filterText)
		{
			if (std::iswspace(ch))
			{
				if (!currentToken.empty())
				{
					state.filterTokens.push_back(currentToken);
					currentToken.clear();
				}
				continue;
			}

			currentToken.push_back((wchar_t)std::towlower(ch));
		}

		if (!currentToken.empty())
		{
			state.filterTokens.push_back(currentToken);
		}
	}

	bool EntityList::DoesEntityMatchFilter(const Entity* entity) const
	{
		if (entity == nullptr)
			return false;

		const auto& state = GetStateConst(this);
		if (state.filterTokens.empty())
			return true;

		std::wstring loweredName(entity->GetName().begin(), entity->GetName().end());
		std::transform(loweredName.begin(), loweredName.end(), loweredName.begin(),
			[](wchar_t ch)
			{
				return (wchar_t)std::towlower(ch);
			});

		for (const auto& token : state.filterTokens)
		{
			if (token.empty())
				continue;

			if (loweredName.find(token) == std::wstring::npos)
				return false;
		}

		return true;
	}
}
