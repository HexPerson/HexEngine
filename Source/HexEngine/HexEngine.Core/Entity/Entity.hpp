

#pragma once

#include "../Required.hpp"
#include "Reflection/IObject.hpp"
#include "../Scene/Model.hpp"
#include "../FileSystem/SceneSaveFile.hpp"
#include "Messaging/MessageListener.hpp"
#include "Messaging/MessageDispatcher.hpp"

#include "Component/ComponentTypes.hpp"

namespace HexEngine
{
	struct GUIDComparer
	{
		bool operator()(const GUID& Left, const GUID& Right) const
		{
			// comparison logic goes here
			return memcmp(&Left, &Right, sizeof(Right)) < 0;
		}
	};

	// {47C5DF48-DA6D-441B-AE08-05DC09095031}
	DEFINE_HEX_GUID(EntityGUID,
		0x47c5df48, 0xda6d, 0x441b, 0xae, 0x8, 0x5, 0xdc, 0x9, 0x9, 0x50, 0x31);

	enum class Layer : uint32_t
	{
		StaticGeometry	= 0,
		DynamicGeometry = 1,
		Invisible		= 2,
		Water			= 3,
		Decorative		= 4,
		Camera			= 5,
		Particle		= 6,
		Trigger			= 7,
		Sky				= 8,
		Grass			= 9,

		CustomLayer = 10,

		AllLayers = 0x7FFFFFFF,
	};

	using LayerMask = uint32_t;
#define LAYERMASK(l) HEX_BITSET((uint32_t)l)

	DEFINE_ENUM_FLAG_OPERATORS(Layer);

	class StaticMeshComponent;
	class Transform;
	class BaseComponent;
	class Chunk;

	enum class EntityFlags
	{
		None = 0,
		HasBeenCreated			= HEX_BITSET(0),
		IsPendingRemoval		= HEX_BITSET(1),
		DoNotSave				= HEX_BITSET(2),
		PreviousTransformDirty	= HEX_BITSET(3),
		DoNotBlockNavMesh		= HEX_BITSET(4),
		DoNotRender				= HEX_BITSET(5),
		SelectedInEditor		= HEX_BITSET(6)
	};
	DEFINE_ENUM_FLAG_OPERATORS(EntityFlags);

	constexpr EntityFlags EntityInvalidMask = (EntityFlags::IsPendingRemoval);

	class HEX_API Entity final : public MessageListener, public Reflection::IObject
	{
	public:
		//DEFINE_OBJECT_GUID(Entity);

		Entity(Scene* scene);

		virtual ~Entity();

		virtual void Destroy();

		virtual void Create();

		void DeleteMe(bool broadcast = true);

		bool IsValid() const;
		bool IsCreated() const;
		bool IsPendingDeletion() const;
		bool HasFlag(EntityFlags flags) const;
		void SetFlag(EntityFlags flags);
		void ClearFlags(EntityFlags flags);
		EntityFlags GetFlags() const;

		virtual void DebugRender();

		Scene* GetScene() const;

		template <typename T>
		bool HasA()
		{
			return (GetComponentSignature() & (1 << T::_GetComponentId())) != 0;
		}

		// Components
		//
		BaseComponent* AddComponent(BaseComponent* component);

		template <class T, typename...Args>
		T* AddComponent(Args... args)
		{
			return reinterpret_cast<T*>(AddComponent(new T(this, args...)));
		}		

		template <typename T>
		T* GetComponent()
		{
			return reinterpret_cast<T*>(GetComponentByID(T::_GetComponentId()));
		}

		template <typename T>
		std::vector<T*> GetComponents()
		{
			std::vector<T*> result;

			for (auto&& component : _components)
			{
				if (component.id == T::_GetComponentId())
					result.push_back(reinterpret_cast<T*>(component.component));
			}

			return result;
		}

		std::vector<BaseComponent*> GetAllComponents()
		{
			std::vector<BaseComponent*> comps;

			for (auto&& comp : _components)
			{
				comps.push_back(comp.component);
			}

			return comps;
		}

		BaseComponent*				GetComponentByID(const ComponentId& id);
		BaseComponent*				GetComponentByClassName(const std::string& name);
		std::vector<BaseComponent*>	GetComponentsBySignature(const ComponentSignature& signature);
		const ComponentSignature&	GetComponentSignature() const;
		void						RemoveComponent(BaseComponent* component);
		void						RemoveComponentById(ComponentId id);

		template <typename T>
		void RemoveComponent()
		{
			RemoveComponentById(T::_GetComponentId());
		}

		void SetName(const std::string& name);
		const std::string& GetName() const;

		Layer GetLayer() const;
		void SetLayer(Layer layer);

		int32_t GetTag() const;
		void SetTag(int32_t tag);

		Entity* GetParent() const;
		void SetParent(Entity* parent);
		const std::vector<Entity*>& GetChildren() const;

		// Helper funcs for transform
		//
		void ForcePosition(const math::Vector3& position);
		void SetPosition(const math::Vector3& position);
		const math::Vector3& GetPosition() const;
		//const math::Vector3 GetRenderPosition() const;

		void SetRotation(const math::Quaternion& rotation);
		void ForceRotation(const math::Quaternion& rotation);
		const math::Quaternion& GetRotation() const;
		//const math::Quaternion GetRenderRotation() const;

		void SetScale(const math::Vector3& scale);
		void ForceScale(const math::Vector3& scale);
		const math::Vector3& GetScale() const;
		math::Vector3 GetAbsoluteScale() const;

		void SetOBB(const dx::BoundingOrientedBox& obb);

		const dx::BoundingBox& GetAABB();
		void SetAABB(const dx::BoundingBox& bbox);
		const dx::BoundingBox& GetWorldAABB();

		void SetOcclusionVolume(const dx::BoundingBox& volume);
		const dx::BoundingBox& GetOcclusionVolume() const;
		const dx::BoundingBox GetWorldOcclusionVolume();
		

		const dx::BoundingOrientedBox& GetOBB();
		const dx::BoundingOrientedBox& GetWorldOBB();
		void RecalculateBoundingVolumes(const dx::BoundingBox& aabb);

		const dx::BoundingSphere& GetBoundingSphere() const;
		const dx::BoundingSphere GetWorldBoundingSphere();	

		bool IsInPVS() const { return _isInPVS; }

		const math::Matrix& GetWorldTM();
		const math::Matrix& GetWorldTMInvert();
		const math::Matrix& GetWorldTMPrev() const;
		const math::Matrix& GetWorldTMPrevTranspose() const;
		const math::Matrix& GetWorldTMTranspose();
		const math::Matrix& GetLocalTM();

		void ClearTransformCache();

		virtual void OnMessage(Message* message, MessageListener* sender) override;
		void BroadcastMessage(Message* message);

		void OnGUI();

		/*virtual const math::Matrix& GetWorldRenderTM();
		virtual const math::Matrix& GetWorldRenderTMTranspose();
		virtual const math::Matrix& GetLocalRenderTM();*/

		//virtual void OnTransformChanged(bool scaleChanged, bool rotationChanged, bool translationChanged);

		void SetCastsShadows(bool receivesShadows);
		bool GetCastsShadows();

		void SetChunk(Chunk* chunk);
		Chunk* GetChunk() const;

		StaticMeshComponent* GetCachedMeshRenderer() { return _cachedMeshRenderer; }

		// saving and loading
		virtual void Serialize(json& data, JsonFile* file) override;
		virtual void Deserialize(json& data, JsonFile* file, uint32_t mask = 0) override;

		static Entity* LoadFromFile(json& data, const std::string& name, Scene* scene, JsonFile* file);

		void ToggleVisibility();

	private:
		struct EntityGuidComponent
		{
			ComponentId id;
			BaseComponent* component;
		};
		std::vector<EntityGuidComponent> _components;
		ComponentSignature _componentsSignature = 0;	

	protected:
		bool _isInPVS = false;
		Scene* _scene = nullptr;
		EntityFlags _flags = EntityFlags::None;

		std::string _name;
		Layer _layer = Layer::StaticGeometry;
		int32_t _tag = 0;

		Entity* _parent = nullptr;
		std::vector<Entity*> _children;

		dx::BoundingBox _aabb;
		dx::BoundingOrientedBox _obb;
		dx::BoundingSphere _boundingSphere;
		dx::BoundingBox _occlusionVolume;

		Chunk* _lastChunk = nullptr;
		math::Vector3 _lastPosition;

		math::Matrix _cachedLocalTM;
		math::Matrix _cachedWorldTM;
		math::Matrix _cachedWorldTMInvert;
		math::Matrix _cachedWorldTMPrev;
		math::Matrix _cachedWorldTMPrevTranspose;
		math::Matrix _cachedWorldTMTranspose;
		dx::BoundingBox _cachedWorldAABB;
		dx::BoundingOrientedBox _cachedWorldOBB;
		dx::BoundingSphere _cachedWorldBoundingSphere;
		dx::BoundingBox _cachedWorldOcclusionVolume;
		bool _hasCachedLocalTM = false;
		bool _hasCachedWorldTM = false;
		bool _hasCachedWorldTMInvert = false;
		bool _hasCachedWorldTMTranspose = false;
		bool _hasCachedWorldAABB = false;
		bool _hasCachedWorldOBB = false;
		bool _hasCachedWorldBoundingSphere = false;
		bool _hasCachedWorldOcclusionVolume = true;
		Transform* _cachedTransform = nullptr;
		StaticMeshComponent* _cachedMeshRenderer = nullptr;
		bool _canCastShadows = true;
		
	};
}
