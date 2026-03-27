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
#include "../Terrain/HeightMapGenerator.hpp"

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
		using EntityVector = std::vector<Entity*>;
		using EntityComponentVector = std::vector<BaseComponent*>;

		using EntityMap = std::map<ComponentSignature, EntityVector>;
		using ComponentMap = std::map<ComponentId, EntityComponentVector>;

		void Create(bool createSkySphere, IEntityListener* listener = nullptr);
		void CreateEmpty(bool createSkySphere, IEntityListener* listener = nullptr);
		void CreateDefaultSunLight();

		void Destroy();

		Entity* CreateEntity(const std::string & name, const math::Vector3& position = math::Vector3::Zero, const math::Quaternion& rotation = math::Quaternion::Identity, const math::Vector3& scale = math::Vector3(1.0f));

		Entity* CloneEntity(Entity* entity, const std::string& name, const math::Vector3& position = math::Vector3::Zero, const math::Quaternion& rotation = math::Quaternion::Identity, const math::Vector3& scale = math::Vector3(1.0f), bool retainHierarchy = true);

		Entity* CloneEntity(Entity* entity, bool retainHierarchy = true);

		std::vector<Entity*> MergeFrom(Scene* scene, std::vector<std::pair<Entity*, Entity*>>* outSourceToMerged = nullptr);

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

		template <typename T>
		bool GetComponents(std::vector<T*>& components)
		{
			std::unique_lock lock(_lock);
			const auto& comps = _components[T::_GetComponentId()];
			components.reserve(comps.size());
			for (auto& it : comps)
			{
				components.push_back((T*)it);
			}
			return components.size() > 0;
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
		void HandlePendingRemovals();
		void HandlePendingAdditions();

		void AddEntityInternal(Entity* entity);
		void RemoveEntityInternal(Entity* entity);

	private:
		std::wstring _name;
		EntityNamingPolicy _namingPolicy = EntityNamingPolicy::AutoRename;

		SceneFlags _flags = (SceneFlags::Updateable | SceneFlags::Renderable | SceneFlags::PostProcessingEnabled);

		EntityMap _entities;
		ComponentMap _components;
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
