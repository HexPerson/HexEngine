
#pragma once

#include <HexEngine.Core\HexEngine.hpp>

namespace HexEditor
{
	enum class IconId
	{
		Entity,
		Folder
	};
	class EntityList : public HexEngine::EntityList
	{
	public:
		EntityList(Element* parent, const HexEngine::Point& position, const HexEngine::Point& size);
		virtual ~EntityList();

		//void AddEntity(HexEngine::Entity* entity);
		//void RemoveEntity(HexEngine::Entity* entity);
		void DuplicateEntity(HexEngine::Entity* entity);

		//void RefreshList();

		void SetEntityParent(HexEngine::Entity* sourceEnt, HexEngine::Entity* targetEnt);

		virtual void Render(HexEngine::GuiRenderer* renderer, uint32_t w, uint32_t h) override;

		virtual void SaveAsPrefab(HexEngine::Entity* entity, HexEngine::FileSystem* fs) override;

	private:
		//bool OnClickEntityInList(TreeList* list, TreeList::Item* item, int32_t mouseButton);
		//bool OnDragAndDropEntity(TreeList* list, TreeList::Item* dragSource, TreeList::Item* dragTarget);

	private:
		//void CreateIcons();

	private:
		//HexEngine::DrawList _drawList;
		std::shared_ptr<HexEngine::ITexture2D> _newFolderImg;
		HexEngine::LineEdit* _entitySearch = nullptr;
	};
}
