#include "RoadComponent.hpp"

#include <HexEngine.Core/Entity/Component/StaticMeshComponent.hpp>
#include <HexEngine.Core/Entity/Component/RigidBody.hpp>
#include <HexEngine.Core/GUI/Elements/ArrayElement.hpp>
#include <HexEngine.Core/GUI/Elements/AssetSearch.hpp>
#include <HexEngine.Core/GUI/Elements/Button.hpp>
#include <HexEngine.Core/GUI/Elements/Checkbox.hpp>
#include <HexEngine.Core/GUI/Elements/ComponentWidget.hpp>
#include <HexEngine.Core/GUI/Elements/DragFloat.hpp>
#include <HexEngine.Core/GUI/Elements/DragInt.hpp>
#include <HexEngine.Core/GUI/Elements/DropDown.hpp>
#include <HexEngine.Core/GUI/Elements/Vector3Edit.hpp>
#include <HexEngine.Core/Scene/Mesh.hpp>

#include <algorithm>
#include <cmath>
#include <random>
#include <unordered_set>

namespace
{
	math::Vector3 ResolveRoadForwardVector(RoadComponent::ForwardAxis axis)
	{
		switch (axis)
		{
		case RoadComponent::ForwardAxis::PositiveX: return math::Vector3::Right;
		case RoadComponent::ForwardAxis::NegativeX: return -math::Vector3::Right;
		case RoadComponent::ForwardAxis::NegativeZ: return -math::Vector3::Forward;
		case RoadComponent::ForwardAxis::PositiveZ:
		default: return math::Vector3::Forward;
		}
	}

	float ResolveHalfWidthAlongSide(const fs::path& meshPath, const math::Vector3& sideAxis)
	{
		if (meshPath.empty())
			return 0.0f;

		auto mesh = HexEngine::Mesh::Create(meshPath);
		if (mesh == nullptr)
			return 0.0f;

		const auto bounds = mesh->GetAABB();
		if (std::abs(sideAxis.x) >= std::abs(sideAxis.z))
			return std::max(bounds.Extents.x, 0.0f);
		return std::max(bounds.Extents.z, 0.0f);
	}

	std::wstring AxisLabel(RoadComponent::ForwardAxis axis)
	{
		switch (axis)
		{
		case RoadComponent::ForwardAxis::PositiveX: return L"+X";
		case RoadComponent::ForwardAxis::NegativeX: return L"-X";
		case RoadComponent::ForwardAxis::NegativeZ: return L"-Z";
		case RoadComponent::ForwardAxis::PositiveZ:
		default: return L"+Z";
		}
	}

	constexpr float HalfTurnRadians = 3.14159265359f;
}

RoadComponent::RoadComponent(HexEngine::Entity* entity) :
	BaseComponent(entity)
{
}

RoadComponent::RoadComponent(HexEngine::Entity* entity, RoadComponent* copy) :
	BaseComponent(entity)
{
	if (copy != nullptr)
	{
		_roadMeshPath = copy->_roadMeshPath;
		_pavementMeshPaths = copy->_pavementMeshPaths;
		_resolvedPavementMeshPath = copy->_resolvedPavementMeshPath;
		_lampPostMeshPath = copy->_lampPostMeshPath;
		_hasLampPost = copy->_hasLampPost;
		_isTwoSided = copy->_isTwoSided;
		_randomPavement = copy->_randomPavement;
		_pavementIndex = copy->_pavementIndex;
		_mirrorOffset = copy->_mirrorOffset;
		_sidewalkOffset = copy->_sidewalkOffset;

		_sectionPrefabPath = copy->_sectionPrefabPath;
		_forwardAxis = copy->_forwardAxis;
		_sectionLength = copy->_sectionLength;
		_sectionEndHeightDelta = copy->_sectionEndHeightDelta;
		_autoGenerateTrafficLanes = copy->_autoGenerateTrafficLanes;
		_laneCurvesJson = copy->_laneCurvesJson;
	}
}

void RoadComponent::Serialize(json& data, HexEngine::JsonFile* file)
{
	file->Serialize(data, "_roadMeshPath", _roadMeshPath.string());
	file->Serialize(data, "_pavementMeshPaths", _pavementMeshPaths);
	file->Serialize(data, "_randomPavement", _randomPavement);
	file->Serialize(data, "_pavementIndex", _pavementIndex);
	file->Serialize(data, "_lampPostMeshPath", _lampPostMeshPath.string());
	file->Serialize(data, "_hasLampPost", _hasLampPost);
	file->Serialize(data, "_isTwoSided", _isTwoSided);
	file->Serialize(data, "_mirrorOffset", _mirrorOffset);
	file->Serialize(data, "_sidewalkOffset", _sidewalkOffset);

	// Legacy placement fields are still serialized for backward compatibility.
	file->Serialize(data, "_sectionPrefabPath", _sectionPrefabPath.string());
	file->Serialize(data, "_forwardAxis", static_cast<int32_t>(_forwardAxis));
	file->Serialize(data, "_sectionLength", _sectionLength);
	file->Serialize(data, "_sectionEndHeightDelta", _sectionEndHeightDelta);
	file->Serialize(data, "_autoGenerateTrafficLanes", _autoGenerateTrafficLanes);
	file->Serialize(data, "_laneCurvesJson", _laneCurvesJson);
}

void RoadComponent::Deserialize(json& data, HexEngine::JsonFile* file, uint32_t mask)
{
	(void)mask;

	std::string roadMeshPath;
	std::string legacyPavementMeshPath;
	std::string lampPostMeshPath;
	file->Deserialize(data, "_roadMeshPath", roadMeshPath);
	file->Deserialize(data, "_pavementMeshPaths", _pavementMeshPaths);
	file->Deserialize(data, "_randomPavement", _randomPavement);
	file->Deserialize(data, "_pavementIndex", _pavementIndex);
	file->Deserialize(data, "_lampPostMeshPath", lampPostMeshPath);
	file->Deserialize(data, "_hasLampPost", _hasLampPost);
	file->Deserialize(data, "_isTwoSided", _isTwoSided);
	file->Deserialize(data, "_mirrorOffset", _mirrorOffset);
	file->Deserialize(data, "_sidewalkOffset", _sidewalkOffset);

	// Backward compatibility for old single-path pavement authoring.
	if (_pavementMeshPaths.empty())
	{
		file->Deserialize(data, "_pavementMeshPath", legacyPavementMeshPath);
		if (!legacyPavementMeshPath.empty())
			_pavementMeshPaths.push_back(fs::path(legacyPavementMeshPath).generic_string());
	}

	_roadMeshPath = roadMeshPath.empty() ? fs::path() : fs::path(roadMeshPath);
	_lampPostMeshPath = lampPostMeshPath.empty() ? fs::path() : fs::path(lampPostMeshPath);

	std::string prefabPath;
	int32_t axis = static_cast<int32_t>(_forwardAxis);

	file->Deserialize(data, "_sectionPrefabPath", prefabPath);
	file->Deserialize(data, "_forwardAxis", axis);
	file->Deserialize(data, "_sectionLength", _sectionLength);
	file->Deserialize(data, "_sectionEndHeightDelta", _sectionEndHeightDelta);
	file->Deserialize(data, "_autoGenerateTrafficLanes", _autoGenerateTrafficLanes);
	file->Deserialize(data, "_laneCurvesJson", _laneCurvesJson);

	_sectionPrefabPath = prefabPath.empty() ? fs::path() : fs::path(prefabPath);
	if (axis < static_cast<int32_t>(ForwardAxis::PositiveX) || axis > static_cast<int32_t>(ForwardAxis::NegativeZ))
		axis = static_cast<int32_t>(ForwardAxis::PositiveZ);
	_forwardAxis = static_cast<ForwardAxis>(axis);

	// Generated road children are transient (DoNotSave), so always rebuild after load.
	// RebuildGeneratedEntities is idempotent and updates children by stable names.
	RebuildGeneratedEntities();
}

bool RoadComponent::CreateWidget(HexEngine::ComponentWidget* widget)
{
	auto* forwardAxis = new HexEngine::DropDown(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 22), L"Forward Axis");
	forwardAxis->SetValue(AxisLabel(_forwardAxis));
	forwardAxis->GetContextMenu()->AddItem(new HexEngine::ContextItem(L"+X",
		[this, forwardAxis](const std::wstring&)
		{
			_forwardAxis = ForwardAxis::PositiveX;
			forwardAxis->SetValue(L"+X");
			RebuildGeneratedEntities();
		}));
	forwardAxis->GetContextMenu()->AddItem(new HexEngine::ContextItem(L"-X",
		[this, forwardAxis](const std::wstring&)
		{
			_forwardAxis = ForwardAxis::NegativeX;
			forwardAxis->SetValue(L"-X");
			RebuildGeneratedEntities();
		}));
	forwardAxis->GetContextMenu()->AddItem(new HexEngine::ContextItem(L"+Z",
		[this, forwardAxis](const std::wstring&)
		{
			_forwardAxis = ForwardAxis::PositiveZ;
			forwardAxis->SetValue(L"+Z");
			RebuildGeneratedEntities();
		}));
	forwardAxis->GetContextMenu()->AddItem(new HexEngine::ContextItem(L"-Z",
		[this, forwardAxis](const std::wstring&)
		{
			_forwardAxis = ForwardAxis::NegativeZ;
			forwardAxis->SetValue(L"-Z");
			RebuildGeneratedEntities();
		}));

	auto applyRoadMesh = [this](HexEngine::AssetSearch*, const HexEngine::AssetSearchResult& result)
		{
			_roadMeshPath = result.assetPath;
			RebuildGeneratedEntities();
		};

	auto applyLampPostMesh = [this](HexEngine::AssetSearch*, const HexEngine::AssetSearchResult& result)
		{
			_lampPostMeshPath = result.assetPath;
			RebuildGeneratedEntities();
		};

	auto* roadMesh = new HexEngine::AssetSearch(
		widget,
		widget->GetNextPos(),
		HexEngine::Point(widget->GetSize().x - 20, 84),
		L"Road Mesh",
		{ HexEngine::ResourceType::Mesh },
		applyRoadMesh);
	roadMesh->SetValue(_roadMeshPath.wstring());

	new HexEngine::ArrayElement<std::string>(
		widget,
		widget->GetNextPos(),
		HexEngine::Point(widget->GetSize().x - 20, 132),
		L"Pavement Meshes",
		_pavementMeshPaths,
		[this](HexEngine::Element* parent, std::string& item, int32_t index)
		{
			auto* meshSearch = new HexEngine::AssetSearch(
				parent,
				HexEngine::Point(0, 0),
				HexEngine::Point(parent->GetSize().x, 22),
				L"Mesh",
				{ HexEngine::ResourceType::Mesh });

			meshSearch->SetValue(std::wstring(item.begin(), item.end()));
			meshSearch->SetOnSelectFn([this, &item](HexEngine::AssetSearch*, const HexEngine::AssetSearchResult& result)
				{
					const fs::path chosen = !result.assetPath.empty() ? result.assetPath : result.absolutePath;
					item = chosen.generic_string();
					RebuildGeneratedEntities();
				});
		},
		[]() -> std::string
		{
			return std::string();
		},
		[](const std::string&, int32_t) -> int32_t
		{
			return 34;
		},
		[](const std::string&, int32_t index) -> std::wstring
		{
			return std::format(L"Pavement {}", index + 1);
		});

	auto* randomPavement = new HexEngine::Checkbox(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Random Pavement", &_randomPavement);
	randomPavement->SetOnCheckFn([this](HexEngine::Checkbox*, bool)
		{
			RebuildGeneratedEntities();
		});

	auto* pavementIndex = new HexEngine::DragInt(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Pavement Index", &_pavementIndex, 0, 1024, 1);
	pavementIndex->SetOnDrag([this](int32_t*, int32_t, int32_t)
		{
			RebuildGeneratedEntities();
		});
	pavementIndex->SetOnInputFn([this](HexEngine::LineEdit*, const std::wstring&)
		{
			RebuildGeneratedEntities();
		});

	auto* lampPostMesh = new HexEngine::AssetSearch(
		widget,
		widget->GetNextPos(),
		HexEngine::Point(widget->GetSize().x - 20, 84),
		L"Lamp Post Mesh",
		{ HexEngine::ResourceType::Mesh },
		applyLampPostMesh);
	lampPostMesh->SetValue(_lampPostMeshPath.wstring());

	auto* hasLampPost = new HexEngine::Checkbox(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Has Lamp Post", &_hasLampPost);
	hasLampPost->SetOnCheckFn([this](HexEngine::Checkbox*, bool)
		{
			RebuildGeneratedEntities();
		});

	auto* isTwoSided = new HexEngine::Checkbox(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Is Two Sided", &_isTwoSided);
	isTwoSided->SetOnCheckFn([this](HexEngine::Checkbox*, bool)
		{
			RebuildGeneratedEntities();
		});

	auto* sidewalkOffset = new HexEngine::DragFloat(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Sidewalk Offset", &_sidewalkOffset, -1000.0f, 1000.0f, 0.05f, 2);
	sidewalkOffset->SetOnDrag([this](float, float, float)
		{
			RebuildGeneratedEntities();
		});
	sidewalkOffset->SetOnInputFn([this](HexEngine::LineEdit*, const std::wstring&)
		{
			RebuildGeneratedEntities();
		});

	const std::wstring mirrorAxisLabels[3] = { L"S", L"Y", L"F" };
	auto* mirrorOffset = new HexEngine::Vector3Edit(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Mirror Offset", &_mirrorOffset,
		[this](const math::Vector3&)
		{
			RebuildGeneratedEntities();
		});
	mirrorOffset->SetAxisLabels(mirrorAxisLabels);

	new HexEngine::Button(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Rebuild Generated Mesh Entities",
		[this](HexEngine::Button*) -> bool
		{
			RebuildGeneratedEntities();
			return true;
		});

	return true;
}

void RoadComponent::RebuildGeneratedEntities()
{
	if (GetEntity() == nullptr || GetEntity()->GetScene() == nullptr)
		return;

	const math::Vector3 forward = ResolveRoadForwardVector(_forwardAxis);
	const math::Vector3 sideAxis =math::Vector3::Up.Cross(forward);
	const fs::path pavementMeshPath = ResolvePavementMeshPath();
	const float roadHalfWidth = ResolveHalfWidthAlongSide(_roadMeshPath, sideAxis);
	const float pavementHalfWidth = ResolveHalfWidthAlongSide(pavementMeshPath, sideAxis);
	const float lampHalfWidth = ResolveHalfWidthAlongSide(_lampPostMeshPath, sideAxis);
	// In two-sided mode both sides are generated symmetrically, so apply a doubled
	// centerline delta to keep the authored offset feeling consistent per side.
	const float effectiveSidewalkOffset = std::max(-1000.0f, _isTwoSided ? (_sidewalkOffset * 2.0f) : _sidewalkOffset);
	const math::Vector3 mirrorOffsetWorld = (sideAxis * _mirrorOffset.x) + (math::Vector3::Up * _mirrorOffset.y) + (forward * _mirrorOffset.z);
	const float roadSideOffset = std::max(0.25f, roadHalfWidth);
	const float basePavementOffsetFromCenter = _isTwoSided ? ((roadHalfWidth * 2.0f) + pavementHalfWidth) : (roadHalfWidth + pavementHalfWidth);
	const float pavementSideOffset = std::max(0.5f, basePavementOffsetFromCenter + effectiveSidewalkOffset);
	const float lampSideOffset = std::max(0.75f, pavementSideOffset + pavementHalfWidth + std::max(0.25f, lampHalfWidth));

	std::unordered_set<std::string> expectedGeneratedChildren;
	if (!_roadMeshPath.empty())
	{
		expectedGeneratedChildren.insert("RoadGen_RoadPrimary");
		if (_isTwoSided)
			expectedGeneratedChildren.insert("RoadGen_RoadMirrored");
	}
	if (!pavementMeshPath.empty())
	{
		expectedGeneratedChildren.insert("RoadGen_PavementPrimary");
		if (_isTwoSided)
			expectedGeneratedChildren.insert("RoadGen_PavementMirrored");
	}
	if (_hasLampPost && !_lampPostMeshPath.empty())
	{
		expectedGeneratedChildren.insert("RoadGen_LampPostPrimary");
		if (_isTwoSided)
			expectedGeneratedChildren.insert("RoadGen_LampPostMirrored");
	}

	// Remove stale generated children (including suffixed duplicates such as
	// RoadGen_RoadPrimary2) while preserving canonical currently-expected names.
	std::vector<HexEngine::Entity*> staleGeneratedChildren;
	for (auto* child : GetEntity()->GetChildren())
	{
		if (child == nullptr || child->IsPendingDeletion())
			continue;

		const auto& childName = child->GetName();
		if (childName.rfind("RoadGen_", 0) != 0)
			continue;

		if (expectedGeneratedChildren.find(childName) == expectedGeneratedChildren.end())
			staleGeneratedChildren.push_back(child);
	}
	for (auto* child : staleGeneratedChildren)
	{
		child->DeleteMe();
	}

	EnsureGeneratedMeshChild("RoadGen_RoadPrimary", _roadMeshPath, -sideAxis * roadSideOffset);

	if (_isTwoSided)
	{
		EnsureGeneratedMeshChild("RoadGen_RoadMirrored", _roadMeshPath, (sideAxis * roadSideOffset) + mirrorOffsetWorld, true);
	}
	else
	{
		RemoveGeneratedChildByName("RoadGen_RoadMirrored");
	}

	EnsureGeneratedMeshChild("RoadGen_PavementPrimary", pavementMeshPath, -sideAxis * pavementSideOffset);
	if (_isTwoSided)
	{
		EnsureGeneratedMeshChild("RoadGen_PavementMirrored", pavementMeshPath, (sideAxis * pavementSideOffset) + mirrorOffsetWorld, true);
	}
	else
	{
		RemoveGeneratedChildByName("RoadGen_PavementMirrored");
	}

	if (_hasLampPost)
	{
		EnsureGeneratedMeshChild("RoadGen_LampPostPrimary", _lampPostMeshPath, -sideAxis * lampSideOffset);

		if (_isTwoSided)
		{
			EnsureGeneratedMeshChild("RoadGen_LampPostMirrored", _lampPostMeshPath, (sideAxis * lampSideOffset) + mirrorOffsetWorld, true);
		}
		else
		{
			RemoveGeneratedChildByName("RoadGen_LampPostMirrored");
		}
	}
	else
	{
		RemoveGeneratedChildByName("RoadGen_LampPostPrimary");
		RemoveGeneratedChildByName("RoadGen_LampPostMirrored");
	}

	RemoveGeneratedChildByName("RoadGen_Crosswalk");
}

fs::path RoadComponent::ResolvePavementMeshPath()
{
	_resolvedPavementMeshPath.clear();
	if (_pavementMeshPaths.empty())
		return {};

	std::vector<fs::path> validPaths;
	validPaths.reserve(_pavementMeshPaths.size());
	for (const auto& pathValue : _pavementMeshPaths)
	{
		if (!pathValue.empty())
		{
			validPaths.emplace_back(pathValue);
		}
	}

	if (validPaths.empty())
		return {};

	if (_randomPavement)
	{
		static thread_local std::mt19937 rng(std::random_device{}());
		std::uniform_int_distribution<size_t> dist(0, validPaths.size() - 1);
		_resolvedPavementMeshPath = validPaths[dist(rng)];
	}
	else
	{
		const size_t clampedIndex = static_cast<size_t>(std::clamp(_pavementIndex, 0, static_cast<int32_t>(validPaths.size() - 1)));
		_resolvedPavementMeshPath = validPaths[clampedIndex];
	}

	return _resolvedPavementMeshPath;
}

void RoadComponent::RemoveGeneratedChildByName(const std::string& childName)
{
	auto* parent = GetEntity();
	auto* scene = parent != nullptr ? parent->GetScene() : nullptr;
	if (parent == nullptr || scene == nullptr)
		return;

	for (auto* child : parent->GetChildren())
	{
		if (child != nullptr && !child->IsPendingDeletion() && child->GetName() == childName)
		{
			child->DeleteMe();
			return;
		}
	}
}

void RoadComponent::RemoveGeneratedChildrenByPrefix(const std::string& prefix)
{
	auto* parent = GetEntity();
	if (parent == nullptr)
		return;

	std::vector<HexEngine::Entity*> toDelete;
	for (auto* child : parent->GetChildren())
	{
		if (child == nullptr || child->IsPendingDeletion())
			continue;

		const auto& name = child->GetName();
		if (name.rfind(prefix, 0) == 0)
		{
			toDelete.push_back(child);
		}
	}

	for (auto* child : toDelete)
	{
		child->DeleteMe();
	}
}

void RoadComponent::EnsureGeneratedMeshChild(const std::string& childName, const fs::path& meshPath, const math::Vector3& localOffset, bool rotate180Y)
{
	auto* parent = GetEntity();
	auto* scene = parent != nullptr ? parent->GetScene() : nullptr;
	if (parent == nullptr || scene == nullptr)
		return;

	if (meshPath.empty())
	{
		RemoveGeneratedChildByName(childName);
		return;
	}

	HexEngine::Entity* child = nullptr;
	for (auto* existing : parent->GetChildren())
	{
		if (existing != nullptr && !existing->IsPendingDeletion() && existing->GetName() == childName)
		{
			child = existing;
			break;
		}
	}

	if (child == nullptr)
	{
		child = scene->CreateEntity(childName, localOffset, math::Quaternion::Identity, math::Vector3(1.0f, 1.0f, 1.0f));
		if (child == nullptr)
			return;

		child->SetParent(parent);
		child->SetFlag(HexEngine::EntityFlags::DoNotSave);
		child->SetFlag(HexEngine::EntityFlags::ExcludeFromHLOD);
	}

	const auto mirrorYaw = math::Quaternion::CreateFromYawPitchRoll(HalfTurnRadians, 0.0f, 0.0f);
	const auto targetLocalRotation = rotate180Y ? mirrorYaw : math::Quaternion::Identity;
	child->ForcePosition(localOffset);
	child->ForceRotation(targetLocalRotation);
	child->SetScale(math::Vector3(1.0f, 1.0f, 1.0f));
	child->SetFlag(HexEngine::EntityFlags::DoNotSave);

	auto* meshComponent = child->GetComponent<HexEngine::StaticMeshComponent>();
	if (meshComponent == nullptr)
	{
		meshComponent = child->AddComponent<HexEngine::StaticMeshComponent>();
	}

	auto mesh = HexEngine::Mesh::Create(meshPath);
	if (mesh == nullptr)
	{
		LOG_WARN("RoadComponent: failed to load mesh '%s' for generated child '%s'.", meshPath.string().c_str(), childName.c_str());
		return;
	}

	meshComponent->SetMesh(mesh);

	auto* rigidBody = child->GetComponent<HexEngine::RigidBody>();
	if (rigidBody == nullptr)
	{
		rigidBody = child->AddComponent<HexEngine::RigidBody>(HexEngine::IRigidBody::BodyType::Static);
	}

	if (rigidBody != nullptr)
	{
		if (auto* body = rigidBody->GetIRigidBody(); body != nullptr)
		{
			body->SetBodyType(HexEngine::IRigidBody::BodyType::Static);
		}

		rigidBody->RemoveCollider();
		rigidBody->AddBoxCollider(child->GetAABB());
	}
}
