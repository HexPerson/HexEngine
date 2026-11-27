#pragma once

#include <unordered_map>
#include <HexEngine.Core/HexEngine.hpp>

class TrafficSpawnerComponent;
class TrafficVehicleComponent;

class TrafficManagerComponent : public HexEngine::UpdateComponent
{
public:
	CREATE_COMPONENT_ID(TrafficManagerComponent);
	DEFINE_COMPONENT_CTOR(TrafficManagerComponent);

	virtual void Update(float frameTime) override;
	virtual void Serialize(json& data, HexEngine::JsonFile* file) override;
	virtual void Deserialize(json& data, HexEngine::JsonFile* file, uint32_t mask = 0) override;
	virtual bool CreateWidget(HexEngine::ComponentWidget* widget) override;

private:
	void UpdateSpawner(TrafficSpawnerComponent* spawner, HexEngine::Camera* camera, float frameTime, int32_t& activeGlobalCount);
	int32_t SpawnVehicleFromSpawner(TrafficSpawnerComponent* spawner);
	int32_t DespawnSpawnerVehicles(TrafficSpawnerComponent* spawner);
	void UpdateNearMisses(HexEngine::Scene* scene);
	// Night headlights (Drop 07): a small pool of shadowless spot lights handed to
	// the cars nearest the camera each frame - per-car lights would swamp the
	// light budget. Created on first use in play, cleared with the play scene.
	void UpdateHeadlights(HexEngine::Scene* scene, HexEngine::Camera* camera);
	// Pooled headlight entities, by NAME: they are runtime (DoNotSave) entities that
	// stopping play destroys while this component lives on, so a raw pointer kept
	// across a play session dangled (crash in UpdateHeadlights on the next play).
	std::vector<std::string> _headlights;
	// Where a vehicle's two headlamps sit, in its own (root-local) space: the
	// front corners of its measured body, cached per vehicle entity name.
	struct LampPair { math::Vector3 left, right; };
	std::unordered_map<std::string, LampPair> _lampCache;
	const LampPair& LampsFor(HexEngine::Entity* vehicle);
	struct NearTrack
	{
		float minGap = 1e9f;
		float closingAtMin = 0.0f;
		bool counted = false;
		bool spoiled = false;   // the rider crashed during this pass
	};
	std::unordered_map<TrafficVehicleComponent*, NearTrack> _nearTracks;

private:
	bool _enabled = true;
	bool _wasGameRunning = false;
	float _nightFactor = 0.0f;   // runtime: 0 day .. 1 night, from the scene clock
	int32_t _globalMaxActiveVehicles = 200;
	float _globalActivationDistance = 500.0f;
	bool _drawDebug = true;
};


