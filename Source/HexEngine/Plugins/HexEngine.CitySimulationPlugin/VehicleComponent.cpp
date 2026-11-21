#include "VehicleComponent.hpp"

#include <HexEngine.Core/Entity/Component/StaticMeshComponent.hpp>
#include <HexEngine.Core/GUI/Elements/ArrayElement.hpp>
#include <HexEngine.Core/GUI/Elements/AssetSearch.hpp>
#include <HexEngine.Core/GUI/Elements/Button.hpp>
#include <HexEngine.Core/GUI/Elements/ComponentWidget.hpp>
#include <HexEngine.Core/GUI/Elements/DragFloat.hpp>
#include <HexEngine.Core/GUI/Elements/DragInt.hpp>
#include <HexEngine.Core/Scene/Mesh.hpp>
#include <HexEngine.Core/Graphics/Material.hpp>

#include <format>

VehicleComponent::VehicleComponent(HexEngine::Entity* entity) :
	BaseComponent(entity)
{
}

VehicleComponent::VehicleComponent(HexEngine::Entity* entity, VehicleComponent* copy) :
	BaseComponent(entity)
{
	if (copy != nullptr)
	{
		_baseMeshPath = copy->_baseMeshPath;
		_doorMeshPaths = copy->_doorMeshPaths;
		_glassMeshPath = copy->_glassMeshPath;
		_trunkMeshPath = copy->_trunkMeshPath;
		_wheelMeshPaths = copy->_wheelMeshPaths;
		_extraMeshPaths = copy->_extraMeshPaths;
		_paintMaterialPath = copy->_paintMaterialPath;
		_paintSourceMaterial = copy->_paintSourceMaterial;
		_maxSpeed = copy->_maxSpeed;
		_acceleration = copy->_acceleration;
		_passengerCapacity = copy->_passengerCapacity;
	}
}

void VehicleComponent::Serialize(json& data, HexEngine::JsonFile* file)
{
	file->Serialize(data, "_baseMeshPath", _baseMeshPath.string());
	file->Serialize(data, "_doorMeshPaths", _doorMeshPaths);
	file->Serialize(data, "_glassMeshPath", _glassMeshPath.string());
	file->Serialize(data, "_trunkMeshPath", _trunkMeshPath.string());
	file->Serialize(data, "_wheelMeshPaths", _wheelMeshPaths);
	file->Serialize(data, "_extraMeshPaths", _extraMeshPaths);
	file->Serialize(data, "_paintMaterialPath", _paintMaterialPath.string());
	file->Serialize(data, "_paintSourceMaterial", _paintSourceMaterial);
	file->Serialize(data, "_maxSpeed", _maxSpeed);
	file->Serialize(data, "_acceleration", _acceleration);
	file->Serialize(data, "_passengerCapacity", _passengerCapacity);
}

void VehicleComponent::Deserialize(json& data, HexEngine::JsonFile* file, uint32_t mask)
{
	(void)mask;

	std::string baseMeshPath;
	std::string glassMeshPath;
	std::string trunkMeshPath;

	file->Deserialize(data, "_baseMeshPath", baseMeshPath);
	file->Deserialize(data, "_doorMeshPaths", _doorMeshPaths);
	file->Deserialize(data, "_glassMeshPath", glassMeshPath);
	file->Deserialize(data, "_trunkMeshPath", trunkMeshPath);
	file->Deserialize(data, "_wheelMeshPaths", _wheelMeshPaths);
	file->Deserialize(data, "_extraMeshPaths", _extraMeshPaths);
	std::string paintMaterialPath;
	file->Deserialize(data, "_paintMaterialPath", paintMaterialPath);
	_paintMaterialPath = paintMaterialPath.empty() ? fs::path() : fs::path(paintMaterialPath);
	file->Deserialize(data, "_paintSourceMaterial", _paintSourceMaterial);
	file->Deserialize(data, "_maxSpeed", _maxSpeed);
	file->Deserialize(data, "_acceleration", _acceleration);
	file->Deserialize(data, "_passengerCapacity", _passengerCapacity);

	_baseMeshPath = baseMeshPath.empty() ? fs::path() : fs::path(baseMeshPath);
	_glassMeshPath = glassMeshPath.empty() ? fs::path() : fs::path(glassMeshPath);
	_trunkMeshPath = trunkMeshPath.empty() ? fs::path() : fs::path(trunkMeshPath);

	RebuildGeneratedEntities();
}

bool VehicleComponent::CreateWidget(HexEngine::ComponentWidget* widget)
{
	auto applyBaseMesh = [this](HexEngine::AssetSearch*, const HexEngine::AssetSearchResult& result)
		{
			_baseMeshPath = result.assetPath;
			RebuildGeneratedEntities();
		};
	auto applyGlassMesh = [this](HexEngine::AssetSearch*, const HexEngine::AssetSearchResult& result)
		{
			_glassMeshPath = result.assetPath;
			RebuildGeneratedEntities();
		};
	auto applyTrunkMesh = [this](HexEngine::AssetSearch*, const HexEngine::AssetSearchResult& result)
		{
			_trunkMeshPath = result.assetPath;
			RebuildGeneratedEntities();
		};

	auto* baseMesh = new HexEngine::AssetSearch(
		widget,
		widget->GetNextPos(),
		HexEngine::Point(widget->GetSize().x - 20, 84),
		L"Base Mesh",
		{ HexEngine::ResourceType::Mesh },
		applyBaseMesh);
	baseMesh->SetValue(_baseMeshPath.wstring());

	new HexEngine::ArrayElement<std::string>(
		widget,
		widget->GetNextPos(),
		HexEngine::Point(widget->GetSize().x - 20, 132),
		L"Door Meshes",
		_doorMeshPaths,
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
			return std::format(L"Door {}", index + 1);
		});

	auto* glassMesh = new HexEngine::AssetSearch(
		widget,
		widget->GetNextPos(),
		HexEngine::Point(widget->GetSize().x - 20, 84),
		L"Glass Mesh",
		{ HexEngine::ResourceType::Mesh },
		applyGlassMesh);
	glassMesh->SetValue(_glassMeshPath.wstring());

	auto* trunkMesh = new HexEngine::AssetSearch(
		widget,
		widget->GetNextPos(),
		HexEngine::Point(widget->GetSize().x - 20, 84),
		L"Trunk Mesh",
		{ HexEngine::ResourceType::Mesh },
		applyTrunkMesh);
	trunkMesh->SetValue(_trunkMeshPath.wstring());

	new HexEngine::ArrayElement<std::string>(
		widget,
		widget->GetNextPos(),
		HexEngine::Point(widget->GetSize().x - 20, 132),
		L"Wheel Meshes",
		_wheelMeshPaths,
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
			return std::format(L"Wheel {}", index + 1);
		});

	auto* paintMaterial = new HexEngine::AssetSearch(
		widget,
		widget->GetNextPos(),
		HexEngine::Point(widget->GetSize().x - 20, 84),
		L"Paint Material",
		{ HexEngine::ResourceType::Material },
		[this](HexEngine::AssetSearch*, const HexEngine::AssetSearchResult& result)
		{
			_paintMaterialPath = result.assetPath;
			RebuildGeneratedEntities();
		});
	paintMaterial->SetValue(_paintMaterialPath.wstring());

	new HexEngine::ArrayElement<std::string>(
		widget,
		widget->GetNextPos(),
		HexEngine::Point(widget->GetSize().x - 20, 132),
		L"Extra Part Meshes",
		_extraMeshPaths,
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
			return std::format(L"Part {}", index + 1);
		});

	auto* maxSpeed = new HexEngine::DragFloat(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Max Speed", &_maxSpeed, 0.0f, 1000.0f, 1.0f, 2);
	maxSpeed->SetOnDrag([this](float, float, float) { RebuildGeneratedEntities(); });
	maxSpeed->SetOnInputFn([this](HexEngine::LineEdit*, const std::wstring&) { RebuildGeneratedEntities(); });

	auto* acceleration = new HexEngine::DragFloat(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Acceleration", &_acceleration, 0.0f, 1000.0f, 0.5f, 2);
	acceleration->SetOnDrag([this](float, float, float) { RebuildGeneratedEntities(); });
	acceleration->SetOnInputFn([this](HexEngine::LineEdit*, const std::wstring&) { RebuildGeneratedEntities(); });

	auto* passengerCapacity = new HexEngine::DragInt(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Passenger Capacity", &_passengerCapacity, 1, 128, 1);
	passengerCapacity->SetOnDrag([this](int32_t*, int32_t, int32_t) { RebuildGeneratedEntities(); });
	passengerCapacity->SetOnInputFn([this](HexEngine::LineEdit*, const std::wstring&) { RebuildGeneratedEntities(); });

	new HexEngine::Button(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Rebuild Generated Mesh Entities",
		[this](HexEngine::Button*) -> bool
		{
			RebuildGeneratedEntities();
			return true;
		});

	return true;
}

void VehicleComponent::RebuildGeneratedEntities()
{
	if (GetEntity() == nullptr || GetEntity()->GetScene() == nullptr)
		return;

	RemoveGeneratedChildrenByPrefix("VehicleGen_");
	_wheelParts.clear();
	_wheelPartsCollected = false;

	std::shared_ptr<HexEngine::Material> paint;
	if (!_paintMaterialPath.empty())
	{
		paint = HexEngine::Material::Create(_paintMaterialPath);
		if (paint == nullptr)
			LOG_WARN("VehicleComponent: failed to load paint material '%s'.", _paintMaterialPath.string().c_str());
	}

	EnsureGeneratedMeshChild("VehicleGen_Base", _baseMeshPath, paint);
	EnsureGeneratedMeshChild("VehicleGen_Glass", _glassMeshPath, paint);
	EnsureGeneratedMeshChild("VehicleGen_Trunk", _trunkMeshPath, paint);

	const auto doorMeshes = ToPathList(_doorMeshPaths);
	for (size_t i = 0; i < doorMeshes.size(); ++i)
	{
		if (doorMeshes[i].empty())
			continue;
		EnsureGeneratedMeshChild(std::format("VehicleGen_Door{}", i + 1), doorMeshes[i], paint);
	}

	const auto wheelMeshes = ToPathList(_wheelMeshPaths);
	for (size_t i = 0; i < wheelMeshes.size(); ++i)
	{
		if (wheelMeshes[i].empty())
			continue;
		EnsureGeneratedMeshChild(std::format("VehicleGen_Wheel{}", i + 1), wheelMeshes[i], paint);
	}

	const auto extraMeshes = ToPathList(_extraMeshPaths);
	for (size_t i = 0; i < extraMeshes.size(); ++i)
	{
		if (extraMeshes[i].empty())
			continue;
		EnsureGeneratedMeshChild(std::format("VehicleGen_Part{}", i + 1), extraMeshes[i], paint);
	}
}

void VehicleComponent::RemoveGeneratedChildrenByPrefix(const std::string& prefix)
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

void VehicleComponent::EnsureGeneratedMeshChild(const std::string& childName, const fs::path& meshPath, const std::shared_ptr<HexEngine::Material>& paint)
{
	auto* parent = GetEntity();
	auto* scene = parent != nullptr ? parent->GetScene() : nullptr;
	if (parent == nullptr || scene == nullptr)
		return;

	if (meshPath.empty())
		return;

	auto mesh = HexEngine::Mesh::Create(meshPath);
	if (mesh == nullptr)
	{
		LOG_WARN("VehicleComponent: failed to load mesh '%s' for generated child '%s'.", meshPath.string().c_str(), childName.c_str());
		return;
	}

	auto* child = scene->CreateEntity(childName, math::Vector3::Zero, math::Quaternion::Identity, math::Vector3(1.0f));
	if (child == nullptr)
		return;

	// Parts are authored in vehicle space: keep the identity LOCAL transform. The
	// default SetParent keeps the world position, which only looked right for
	// vehicles spawned at the origin; a vehicle deserialized in place (scene load,
	// e.g. parked cars) got every part re-expressed relative to the world origin.
	child->SetParent(parent, false);
	child->SetFlag(HexEngine::EntityFlags::DoNotSave);
	child->SetFlag(HexEngine::EntityFlags::ExcludeFromHLOD);

	auto* meshComponent = child->AddComponent<HexEngine::StaticMeshComponent>();
	meshComponent->SetExcludeFromGI(true);
	meshComponent->SetMesh(mesh);

	if (paint != nullptr && !_paintSourceMaterial.empty())
	{
		auto current = meshComponent->GetMaterial();
		if (current != nullptr && current->GetFileSystemPath().stem().string() == _paintSourceMaterial)
			meshComponent->SetMaterial(paint);
	}
}

void VehicleComponent::CollectWheelParts()
{
	_wheelPartsCollected = true;
	_wheelParts.clear();
	auto* entity = GetEntity();
	if (entity == nullptr)
		return;
	// Clones (prefab spawns) carry the wheel children without having run
	// RebuildGeneratedEntities, so discover them by name.
	for (auto* child : entity->GetChildren())
	{
		if (child == nullptr || child->IsPendingDeletion() || child->GetName().rfind("VehicleGen_Wheel", 0) != 0)
			continue;
		auto* meshComponent = child->GetComponent<HexEngine::StaticMeshComponent>();
		auto mesh = meshComponent != nullptr ? meshComponent->GetMesh() : nullptr;
		if (mesh == nullptr)
			continue;
		const auto& aabb = mesh->GetAABB();
		WheelPart part;
		part.id = child->GetId();
		part.hub = math::Vector3(aabb.Center.x, aabb.Center.y, aabb.Center.z);
		part.radius = std::max(aabb.Extents.y, 0.1f);
		_wheelParts.push_back(part);
	}
}

void VehicleComponent::SpinWheels(float distance)
{
	if (!_wheelPartsCollected)
		CollectWheelParts();
	if (_wheelParts.empty() || distance == 0.0f)
		return;
	auto* scene = GetEntity() != nullptr ? GetEntity()->GetScene() : nullptr;
	if (scene == nullptr)
		return;

	// One shared angle (all the kit's wheels are the same size per vehicle);
	// rolling forward (+Z) turns the top of the wheel forward: +rotation about +X.
	const float radius = _wheelParts.front().radius;
	_wheelAngle = std::fmod(_wheelAngle + distance / radius, 6.28318530718f);
	const math::Quaternion spin = math::Quaternion::CreateFromAxisAngle(math::Vector3(1.0f, 0.0f, 0.0f), _wheelAngle);

	for (const auto& part : _wheelParts)
	{
		auto* wheel = scene->TryGetEntity(part.id);
		if (wheel == nullptr || wheel->IsPendingDeletion())
		{
			_wheelPartsCollected = false;	// rebuilt/removed: rediscover next time
			continue;
		}
		// Rotate about the hub: p' = hub + R * (p - hub)  =>  local offset = hub - R * hub.
		wheel->SetRotation(spin);
		wheel->SetPosition(part.hub - math::Vector3::Transform(part.hub, spin));
	}
}

std::vector<fs::path> VehicleComponent::ToPathList(const std::vector<std::string>& paths)
{
	std::vector<fs::path> result;
	result.reserve(paths.size());
	for (const auto& path : paths)
	{
		result.emplace_back(path.empty() ? fs::path() : fs::path(path));
	}
	return result;
}

