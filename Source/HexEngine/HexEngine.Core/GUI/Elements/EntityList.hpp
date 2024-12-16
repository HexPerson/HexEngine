
#pragma once

#include "TreeList.hpp"
#include "ContextMenu.hpp"

namespace HexEngine
{
	class Scene;

	enum class IconId
	{
		Entity,
		Folder,
		Scene
	};

	class EntityList : public HexEngine::TreeList
	{
	public:
		using OnEntityClickedFn = std::function<void(EntityList*, Entity*)>;
		using OnSceneClickedFn = std::function<void(EntityList*, Scene*)>;
		using OnEntityParentedFn = std::function<void(EntityList*, Entity*, Entity*)>;

		EntityList(Element* parent, const HexEngine::Point& position, const HexEngine::Point& size);
		virtual ~EntityList();

		void AddEntity(Entity* entity, ListNode* scene);
		void AddEntity(Entity* entity);
		void RemoveEntity(Entity* entity);
		void DuplicateEntity(Entity* entity);
		ListNode* AddScene(Scene* scene);
		virtual void SaveAsPrefab(Entity* entity, FileSystem* fs);

		virtual bool OnInputEvent(HexEngine::InputEvent event, HexEngine::InputData* data) override;

		void RefreshList();

		virtual void Render(GuiRenderer* renderer, uint32_t w, uint32_t h) override;

	private:
		void OnClickEntityInList(ListNode* item, int32_t mouseButton);
		bool OnDragAndDropEntity(TreeList* list, ListNode* dragSource, ListNode* dragTarget);
		void OnLoadScene(SceneListNode* node);

	private:
		void CreateIcons();

	private:
		std::map<IconId, HexEngine::ITexture2D*> _icons;
		HexEngine::ContextMenu* _ctx = nullptr;

	public:
		OnEntityClickedFn _onEntityClicked;
		OnEntityParentedFn _onEntityParented;
		OnSceneClickedFn _onSceneClicked;
	};
}
