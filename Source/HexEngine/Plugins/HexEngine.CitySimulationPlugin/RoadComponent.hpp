#pragma once

#include <HexEngine.Core/HexEngine.hpp>

class RoadComponent : public HexEngine::BaseComponent
{
public:
	enum class ForwardAxis : int32_t
	{
		PositiveX = 0,
		NegativeX = 1,
		PositiveZ = 2,
		NegativeZ = 3,
	};

	CREATE_COMPONENT_ID(RoadComponent);
	DEFINE_COMPONENT_CTOR(RoadComponent);

	virtual void Serialize(json& data, HexEngine::JsonFile* file) override;
	virtual void Deserialize(json& data, HexEngine::JsonFile* file, uint32_t mask = 0) override;
	virtual bool CreateWidget(HexEngine::ComponentWidget* widget) override;

	const fs::path& GetSectionPrefabPath() const { return _sectionPrefabPath; }
	ForwardAxis GetForwardAxis() const { return _forwardAxis; }
	float GetSectionLength() const { return _sectionLength; }
	float GetSectionEndHeightDelta() const { return _sectionEndHeightDelta; }
	bool GetAutoGenerateTrafficLanes() const { return _autoGenerateTrafficLanes; }
	const fs::path& GetRoadMeshPath() const { return _roadMeshPath; }
	const fs::path& GetPavementMeshPath() const { return _resolvedPavementMeshPath; }
	const std::vector<std::string>& GetPavementMeshPaths() const { return _pavementMeshPaths; }
	const fs::path& GetLampPostMeshPath() const { return _lampPostMeshPath; }
	bool GetHasLampPost() const { return _hasLampPost; }
	bool GetIsTwoSided() const { return _isTwoSided; }
	bool GetRandomPavement() const { return _randomPavement; }
	int32_t GetPavementIndex() const { return _pavementIndex; }
	const math::Vector3& GetMirrorOffset() const { return _mirrorOffset; }
	float GetSidewalkOffset() const { return _sidewalkOffset; }

	void RebuildGeneratedEntities();

private:
	void RemoveGeneratedChildByName(const std::string& childName);
	void RemoveGeneratedChildrenByPrefix(const std::string& prefix);
	void EnsureGeneratedMeshChild(const std::string& childName, const fs::path& meshPath, const math::Vector3& localOffset, bool rotate180Y = false);
	fs::path ResolvePavementMeshPath();

private:
	fs::path _roadMeshPath;
	std::vector<std::string> _pavementMeshPaths;
	fs::path _resolvedPavementMeshPath;
	fs::path _lampPostMeshPath;
	bool _hasLampPost = true;
	bool _isTwoSided = true;
	bool _randomPavement = true;
	int32_t _pavementIndex = 0;
	math::Vector3 _mirrorOffset = math::Vector3::Zero;
	float _sidewalkOffset = 0.0f;

	fs::path _sectionPrefabPath;
	ForwardAxis _forwardAxis = ForwardAxis::PositiveZ;
	float _sectionLength = 10.0f;
	float _sectionEndHeightDelta = 0.0f;
	bool _autoGenerateTrafficLanes = true;
	std::string _laneCurvesJson = "[]";
};
