#pragma once

#include <HexEngine.Core/HexEngine.hpp>

// Moves its entity along a waypoint path at a steady speed: boats circling the
// harbour, a train on its line, anything that should just keep going. The path
// is a separate entity whose CHILDREN are the waypoints, taken in name order
// (WP_00, WP_01, ...), so the path doesn't move with the mover and several
// movers can share one path (spread them with _startFraction).
// Optional water motion: a gentle bob and roll. Only runs while the game is
// running (or in the editor with _previewInEditor); play-stop restores the
// authored pose through the play-mode snapshot.
class PathMoverComponent : public HexEngine::UpdateComponent
{
public:
	CREATE_COMPONENT_ID(PathMoverComponent);
	DEFINE_COMPONENT_CTOR(PathMoverComponent);

	virtual void Update(float frameTime) override;
	virtual void Serialize(json& data, HexEngine::JsonFile* file) override;
	virtual void Deserialize(json& data, HexEngine::JsonFile* file, uint32_t mask = 0) override;
	virtual bool CreateWidget(HexEngine::ComponentWidget* widget) override;
	virtual void OnRenderEditorGizmo(bool isSelected, bool& isHovering) override;

private:
	bool RebuildPath();
	math::Vector3 SampleAt(float distance) const;

	std::string _pathEntityName;
	float _speed = 4.0f;               // m/s
	bool _closedLoop = true;           // false: ping-pong between the ends
	float _startFraction = 0.0f;       // 0..1 along the path
	float _yawOffsetDegrees = 0.0f;    // model forward vs +Z
	float _turnRate = 1.5f;            // heading smoothing (1/s)
	float _lookAhead = 6.0f;           // metres ahead used for the heading
	float _bobAmplitude = 0.0f;        // m
	float _bobFrequency = 0.25f;       // Hz
	float _rollAmplitude = 0.0f;       // degrees
	bool _previewInEditor = false;

	// runtime
	std::vector<math::Vector3> _points;
	std::vector<float> _cumulative;    // distance at each point (closed: + closing segment)
	float _length = 0.0f;
	float _distance = 0.0f;
	float _direction = 1.0f;
	float _yaw = 0.0f;
	float _time = 0.0f;
	bool _started = false;
};
