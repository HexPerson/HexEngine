
#include "EntitySelector.hpp"
#include "../../Scene/SceneManager.hpp"

namespace HexEngine
{
	EntitySelector::EntitySelector(Element* parent, const Point& position, const Point& size, const std::wstring& label, ComponentSignature componentMask) :
		Dialog(parent, position, size, label),
		_componentMask(componentMask)
	{
		_list = new EntityList(this, Point(5, 5), Point(size.x - 10, size.y - 10));

		Scene::EntityComponentVector components;
		//if (g_pEnv->_sceneManager->GetCurrentScene()->GetComponents(componentMask, components))
		{
			for (auto& comp : components)
			{
				_list->AddEntity(comp->GetEntity());
			}
		}
	}

	EntityList* EntitySelector::GetList() const
	{
		return _list;
	}
}