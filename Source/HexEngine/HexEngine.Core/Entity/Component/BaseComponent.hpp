

#pragma once

#include "../Reflection/IObject.hpp"
#include "../Messaging/MessageListener.hpp"
#include "ComponentTypes.hpp"

namespace HexEngine
{
	class Entity;
	class GuiRenderer;
	class Style;
	class ComponentWidget;

#define DEFINE_COMPONENT_CTOR(comp) comp(Entity* entity);\
	comp(Entity* entity, comp* copy);

//#define DESERIALIZE_VALUE(name, defaultValue) if (auto it = data.find(#name); it == data.end())\
//	{\
//		name = defaultValue;\
//	}\
//else\
//	{\
//		name = it.value();\
//	}

#define DESERIALIZE_VALUE(name, defaultValue) file->Deserialize(data, #name, name);

#define DESERIALIZE_VALUE_TYPE(name, defaultValue, type) if (auto it = data.find(#name); it == data.end())\
	{\
		name = (type)defaultValue;\
	}\
else\
	{\
		name = (type)it.value();\
	}

#define SERIALIZE_VALUE(name) file->Serialize(data,#name, name);//data[#name] = name;

	class BaseComponent : public MessageListener, public Reflection::IObject
	{
	public:
		BaseComponent(Entity* entity);		

		virtual ~BaseComponent() {}

		virtual void Destroy() {};

		virtual Entity* GetEntity() const;

		virtual ComponentId GetComponentId() = 0;

		virtual const std::string GetComponentName() = 0;

		virtual void DebugRender() {};

		virtual void OnGUI() {}

		template<typename T>
		T* CastAs()
		{
			return dynamic_cast<T*>(this);
		}

		virtual void Serialize(json& data, JsonFile* file) override {};

		virtual void Deserialize(json& data, JsonFile* file, uint32_t mask = 0) override {};

		virtual bool CreateWidget(ComponentWidget* widget) { return false; };

		void BroadcastMessage(Message* message);

	protected:
		ComponentId _componentId = -1;

	private:		
		Entity* _entity = nullptr;
	};
}
