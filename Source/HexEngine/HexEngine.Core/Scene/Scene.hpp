#pragma once

#include "../Required.hpp"
#include "../Entity/Entity.hpp"
#include "../Entity/Component/Camera.hpp"
#include "../Entity/Component/Light.hpp"
#include "../Entity/Component/DirectionalLight.hpp"
#include "IEntityListener.hpp"
#include "../Graphics/IGraphicsDevice.hpp"
#include "../GUI/DebugGUI.hpp"
#include "../Entity/Component/StaticMeshComponent.hpp"
#include "../Entity/Component/UpdateComponent.hpp"
#include "../Terrain/HeightMapGenerator.hpp"
#include <limits>
#include <type_traits>

namespace HexEngine
{
	enum class SceneFlags
	{
		Disabled				= HEX_BITSET(0),
		Updateable				= HEX_BITSET(1),
		Renderable				= HEX_BITSET(2),
		PostProcessingEnabled	= HEX_BITSET(3),
		Utility					= HEX_BITSET(4)
	};

	DEFINE_ENUM_FLAG_OPERATORS(SceneFlags);

	class Camera;

	/*struct SceneRenderParameters
	{
		math::Vector3 cameraLocation;
		math::Matrix viewMatrix;
		math::Matrix projectionMatrix;
		math::Matrix lightViewMatrix;
		math::Matrix lightProjectionMatrix[4];
		math::Vector3 lightPosition;
		math::Vector3 lightDirection;
		float frustumSplits[4];
		Camera* camera = nullptr;
		dx::BoundingSphere frustumSliceBounds;
		
		bool isShadowPass;
		float lightMultiplier;
		int passIndex;
	};*/

	

	enum class EntityNamingPolicy
	{
		AutoRename,
		Singlular,
	};

	struct VisibilitySpheres
	{
		bool needsRebuild = true;
		dx::BoundingSphere shape;
	};

	enum SceneUpdateFlags
	{
		SceneUpdateNone				= 0,
		SceneUpdateAddedEntity		= HEX_BITSET(0),
		SceneUpdateRemovedEntity	= HEX_BITSET(1),
		SceneUpdateCameraMoved		= HEX_BITSET(2),
		SceneUpdateHasCalcedVis		= HEX_BITSET(3)
	};

	DEFINE_ENUM_FLAG_OPERATORS(SceneUpdateFlags);

	

	class HEX_API Scene : public IDebugGUICallback, public IResource
	{
	public:
		static constexpr uint32_t InvalidDenseIndex = std::numeric_limits<uint32_t>::max();

		using EntityVector = std::vector<Entity*>;
		using EntityComponentVector = std::vector<BaseComponent*>;

		using EntityMap = std::map<ComponentSignature, EntityVector>;

		void Create(bool createSkySphere, IEntityListener* listener = nullptr);
		void CreateEmpty(bool createSkySphere, IEntityListener* listener = nullptr);
		void CreateDefaultSunLight();

		void Destroy();

		EntityId CreateEntityId(const std::string& name, const math::Vector3& position = math::Vector3::Zero, const math::Quaternion& rotation = math::Quaternion::Identity, const math::Vector3& scale = math::Vector3(1.0f));
		Entity* CreateEntity(const std::string & name, const math::Vector3& position = math::Vector3::Zero, const math::Quaternion& rotation = math::Quaternion::Identity, const math::Vector3& scale = math::Vector3(1.0f));
		Entity* TryGetEntity(EntityId id) const;
		bool IsValid(EntityId id) const;

		Entity* CloneEntity(Entity* entity, const std::string& name, const math::Vector3& position = math::Vector3::Zero, const math::Quaternion& rotation = math::Quaternion::Identity, const math::Vector3& scale = math::Vector3(1.0f), bool retainHierarchy = true);

		Entity* CloneEntity(Entity* entity, bool retainHierarchy = true);

		std::vector<Entity*> MergeFrom(Scene* scene, std::vector<std::pair<Entity*, Entity*>>* outSourceToMerged = nullptr);

		void DestroyEntity(EntityId entityId, bool broadcast = true);
		void DestroyEntity(Entity* entity, bool broadcast = true);

		void OnEntityAddComponent(Entity* entity, ComponentSignature previousSignature, BaseComponent* component);
		void OnEntityRemoveComponent(Entity* entity, ComponentSignature previousSignature, BaseComponent* component);

		void Clear();

		void Update(float frameTime);

		void FixedUpdate(float frameTime);

		void LateUpdate(float frameTime);

		void Lock();
		void Unlock();
		bool TryLock();

		void FlushPVS(Entity* entity, bool remove=false);
		void ForceRebuildPVS();

		void RenderEntities(PVS* pvs, LayerMask layerMask, MeshRenderFlags renderFlags);

		void OnGUI();

		//void RenderWater(const SceneRenderParameters& params, bool maskPass, ITexture2D* maskTexture);

		//void RenderTransparent(const SceneRenderParameters& params);

		//void RenderParticles(const SceneRenderParameters& params);

		void RenderDebug(PVS* pvs);

		Entity* GetEntityByName(const std::string& name);
		bool RenameEntity(Entity* entity, const std::string& desiredName, std::string* outFinalName = nullptr);

		uint32_t GetNumberOfComponentsOfType(const ComponentId id);

		SceneFlags GetFlags();

		Camera* GetCameraAtIndex(uint32_t index);

		Camera* GetMainCamera();

		void SetMainCamera(Camera* camera);

		void SetSkySphere(Entity* skySphere);

		void CalculateBounds(math::Vector3& min, math::Vector3& max);
		bool GatherStaticMeshesInBounds(const dx::BoundingBox& bounds, std::vector<StaticMeshComponent*>& outComponents, bool includeDynamic = true);
		void CalculateSceneStats(std::vector<math::Vector3>& vertices, std::vector<uint16_t>& indices, uint32_t& numFaces, EntityFlags excludeFlags = EntityFlags::None);
		void CalculateSceneStats_UInt32(std::vector<math::Vector3>& vertices, std::vector<uint32_t>& indices, uint32_t& numFaces, EntityFlags excludeFlags = EntityFlags::None);

		const EntityMap& GetEntities() const;

		bool GetEntities(const ComponentSignature signature, std::vector<Entity*>& entities);
		bool GetLiveEntityIds(std::vector<EntityId>& entityIds);

		template <typename T>
		bool GetComponents(std::vector<T*>& components)
		{
			std::unique_lock lock(_lock);
			if constexpr (std::is_same_v<T, UpdateComponent>)
			{
				components.reserve(_updateComponents.size());
				for (auto* component : _updateComponents)
				{
					components.push_back(component);
				}
				return !components.empty();
			}

			const auto* pool = TryGetComponentPool(T::_GetComponentId());
			if (pool == nullptr || pool->components.empty())
				return false;

			components.reserve(pool->components.size());
			for (auto* component : pool->components)
			{
				components.push_back(static_cast<T*>(component));
			}
			return components.size() > 0;
		}

		template <typename T>
		T* GetComponent(EntityId entityId)
		{
			std::unique_lock lock(_lock);
			const auto* pool = TryGetComponentPool(T::_GetComponentId());
			if (pool == nullptr)
				return nullptr;

			return static_cast<T*>(pool->Get(entityId));
		}

		template <typename T>
		bool HasComponent(EntityId entityId)
		{
			return GetComponent<T>(entityId) != nullptr;
		}

		uint32_t GetTotalNumberOfEntities();

		//void GetLights(std::vector<Light*>& lights) const;

		DirectionalLight* GetSunLight();

		void SetFogColour(const math::Color& colour);
		void SetAmbientLight(const math::Vector4& ambient);
		const math::Color& GetFogColour() const;
		const math::Vector4& GetAmbientColour() const;

		//void RenderSkySphere();

		uint32_t GetNumberOfEntitiesDrawn();
		uint32_t GetDrawCalls();

		void AddEntityListener(IEntityListener* listener);
		void RemoveEntityListener(IEntityListener* listener);

		OceanSettings& GetOcean() { return _oceanSettings; }

		void UpdateSkySphereMatrix();

		void SetEntityNamingPolicy(EntityNamingPolicy policy);

		virtual void OnDebugGUI() override;

		void SetFlags(SceneFlags flags);

		//void PushTerrainParams(const TerrainGenerationParams& params);
		//const std::vector<TerrainGenerationParams>& GetTerrainParams() const;
		//void ClearTerrainParams();

		void RegisterMessageListener(MessageListener* listener);
		void UnregisterMessageListener(MessageListener* listener);
		void BroadcastMessage(Message* message);

		void Save(json& key, JsonFile* file);
		void Load(json& key, JsonFile* file);

		const std::wstring& GetName() const;
		void SetName(const std::wstring& name);

		bool DidAnyDrawnItemReflect() const { return _didAnyDrawnItemReflect; }

		bool DidPvsReset() const { return _wasPvsReset; }

	private:
		struct ComponentPool
		{
			std::vector<BaseComponent*> components;
			std::vector<EntityId> owners;
			std::vector<uint32_t> sparseEntityToDense;

			void EnsureEntityCapacity(uint32_t slotCount);
			BaseComponent* Get(EntityId id) const;
			bool Has(EntityId id) const;
			uint32_t Add(EntityId owner, BaseComponent* component);
			bool Remove(EntityId owner, BaseComponent** outRemoved = nullptr, EntityId* outMovedOwner = nullptr, uint32_t* outMovedDenseIndex = nullptr);
		};

		struct EntitySlot
		{
			// Lifetime phases:
			// 1) allocated-not-live: alive=true, inLiveList=false, denseEntityIndex=InvalidDenseIndex.
			//    Components may already be attached during CreateEntityId before AddEntityInternal.
			// 2) live: alive=true, inLiveList=true, denseEntityIndex points into _liveEntities.
			// 3) free: alive=false, entity=nullptr, inLiveList=false, denseEntityIndex=InvalidDenseIndex.
			uint32_t generation = 1;
			uint32_t denseEntityIndex = InvalidDenseIndex;
			Entity* entity = nullptr;
			bool alive = false;
			bool inLiveList = false;
			std::vector<uint32_t> componentDenseIndices;
		};

		void HandlePendingRemovals();
		void HandlePendingAdditions();
		EntityId AllocateEntityId(Entity* entity);
		void FreeEntityId(EntityId id);
		void EnsureSlotComponentCapacity(EntitySlot& slot, ComponentId componentId);
		ComponentPool* GetOrCreateComponentPool(ComponentId componentId);
		ComponentPool* TryGetComponentPool(ComponentId componentId);
		const ComponentPool* TryGetComponentPool(ComponentId componentId) const;
		void MarkEntityViewDirty();
		void RebuildEntityViewCache() const;
		void AddUpdateComponent(UpdateComponent* component);
		void RemoveUpdateComponent(UpdateComponent* component);
		void ValidateInvariants_NoLock() const;

		void AddEntityInternal(Entity* entity);
		void RemoveEntityInternal(Entity* entity);

	private:
		std::wstring _name;
		EntityNamingPolicy _namingPolicy = EntityNamingPolicy::AutoRename;

		SceneFlags _flags = (SceneFlags::Updateable | SceneFlags::Renderable | SceneFlags::PostProcessingEnabled);

		mutable EntityMap _entities;
		mutable bool _entityViewDirty = true;
		std::vector<EntitySlot> _entitySlots;
		std::vector<uint32_t> _freeEntitySlotIndices;
		std::vector<EntityId> _liveEntities;
		std::unordered_map<ComponentId, ComponentPool> _componentPools;
		std::vector<UpdateComponent*> _updateComponents;
		std::unordered_map<UpdateComponent*, uint32_t> _updateComponentIndices;
		std::map<std::string, Entity*> _entNameMap;

		std::set<Entity*> _pendingAdditions;
		std::set<Entity*> _pendingRemovals;

		volatile bool _insideEntityIteration = false;
		uint32_t _drawnEntities = 0;
		uint32_t _drawCalls = 0;

		DirectionalLight* _sunLight = nullptr;

		Camera* _mainCamera = nullptr;
		std::vector<Camera*> _cameras;

		SceneUpdateFlags _updateFlags = SceneUpdateNone;

		math::Color _fogColour;
		Entity* _skySphere = nullptr;

		std::thread _updateThread;

		math::Vector4 _ambientLight;

		std::vector<IEntityListener*> _entityListeners;

		OceanSettings _oceanSettings;

		std::recursive_mutex _lock;

		std::vector<MessageListener*> _auxMessageListeners;

		bool _didAnyDrawnItemReflect = false;
		bool _wasPvsReset = true;
		
	};
}
