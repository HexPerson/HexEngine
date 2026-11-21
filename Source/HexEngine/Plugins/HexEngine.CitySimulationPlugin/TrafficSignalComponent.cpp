#include "TrafficSignalComponent.hpp"
#include "TrafficSignalControllerComponent.hpp"
#include <HexEngine.Core/Entity/Entity.hpp>
#include <HexEngine.Core/Entity/Component/StaticMeshComponent.hpp>
#include <HexEngine.Core/Graphics/Material.hpp>
#include <HexEngine.Core/GUI/Elements/ComponentWidget.hpp>
#include <HexEngine.Core/GUI/Elements/DragInt.hpp>
#include <HexEngine.Core/GUI/Elements/LineEdit.hpp>
#include <HexEngine.Core/Scene/Mesh.hpp>
#include <HexEngine.Core/Scene/Scene.hpp>

namespace
{
	// Visual states: vehicle heads use the SignalState values (0 red, 1 amber,
	// 2 green); pedestrian heads use these two.
	constexpr int32_t kVisualDontWalk = 10;
	constexpr int32_t kVisualWalk = 11;
}

TrafficSignalComponent::TrafficSignalComponent(HexEngine::Entity* entity) :
	UpdateComponent(entity)
{
}

TrafficSignalComponent::TrafficSignalComponent(HexEngine::Entity* entity, TrafficSignalComponent* copy) :
	UpdateComponent(entity)
{
	if (copy != nullptr)
	{
		_controllerEntityName = copy->_controllerEntityName;
		_arm = copy->_arm;
		_role = copy->_role;
		_redMaterial = copy->_redMaterial;
		_amberMaterial = copy->_amberMaterial;
		_greenMaterial = copy->_greenMaterial;
		_walkMesh = copy->_walkMesh;
		_dontWalkMesh = copy->_dontWalkMesh;
		_walkMaterial = copy->_walkMaterial;
		_dontWalkMaterial = copy->_dontWalkMaterial;
	}
}

void TrafficSignalComponent::SetController(const std::string& controllerEntityName, const std::string& arm)
{
	_controllerEntityName = controllerEntityName;
	_arm = arm;
	_appliedState = -1;
}

TrafficSignalControllerComponent* TrafficSignalComponent::ResolveController(std::string& outArm) const
{
	auto* entity = GetEntity();
	auto* scene = entity != nullptr ? entity->GetScene() : nullptr;
	if (scene == nullptr)
		return nullptr;

	if (!_controllerEntityName.empty())
	{
		outArm = _arm;
		auto* controllerEntity = scene->GetEntityByName(_controllerEntityName);
		return controllerEntity != nullptr ? controllerEntity->GetComponent<TrafficSignalControllerComponent>() : nullptr;
	}

	// Inherit from the nearest wired signal up the hierarchy (e.g. a pedestrian
	// head mounted on a vehicle signal post).
	for (auto* parent = entity->GetParent(); parent != nullptr; parent = parent->GetParent())
	{
		if (auto* parentSignal = parent->GetComponent<TrafficSignalComponent>(); parentSignal != nullptr && parentSignal != this)
			return parentSignal->ResolveController(outArm);
	}
	return nullptr;
}

void TrafficSignalComponent::Update(float frameTime)
{
	UpdateComponent::Update(frameTime);

	std::string arm;
	auto* controller = ResolveController(arm);
	if (controller == nullptr)
		return;

	int32_t visual;
	if (_role == (int32_t)Role::Pedestrian)
		visual = controller->IsPedestrianWalk() ? kVisualWalk : kVisualDontWalk;
	else
		visual = (int32_t)controller->GetArmState(arm.empty() ? 'N' : arm[0]);

	if (visual != _appliedState)
		ApplyVisual(visual);
}

void TrafficSignalComponent::ApplyVisual(int32_t visualState)
{
	auto* entity = GetEntity();
	auto* meshComponent = entity != nullptr ? entity->GetComponent<HexEngine::StaticMeshComponent>() : nullptr;
	if (meshComponent == nullptr)
		return;

	// The lit state changes every few seconds; keep these props out of the
	// voxel GI so the material swaps don't invalidate it.
	if (!_giExcluded)
	{
		meshComponent->SetExcludeFromGI(true);
		_giExcluded = true;
	}

	std::string meshPath;
	std::string materialPath;
	switch (visualState)
	{
	case (int32_t)TrafficSignalControllerComponent::SignalState::Red:   materialPath = _redMaterial; break;
	case (int32_t)TrafficSignalControllerComponent::SignalState::Amber: materialPath = _amberMaterial; break;
	case (int32_t)TrafficSignalControllerComponent::SignalState::Green: materialPath = _greenMaterial; break;
	case kVisualWalk:     meshPath = _walkMesh;     materialPath = _walkMaterial; break;
	case kVisualDontWalk: meshPath = _dontWalkMesh; materialPath = _dontWalkMaterial; break;
	default: break;
	}

	if (!meshPath.empty())
	{
		if (auto mesh = HexEngine::Mesh::Create(meshPath); mesh != nullptr)
			meshComponent->SetMesh(mesh);
	}
	if (!materialPath.empty())
	{
		if (auto material = HexEngine::Material::Create(materialPath); material != nullptr)
			meshComponent->SetMaterial(material);
	}
	_appliedState = visualState;
}

void TrafficSignalComponent::Serialize(json& data, HexEngine::JsonFile* file)
{
	file->Serialize(data, "_controllerEntityName", _controllerEntityName);
	file->Serialize(data, "_arm", _arm);
	file->Serialize(data, "_role", _role);
	file->Serialize(data, "_redMaterial", _redMaterial);
	file->Serialize(data, "_amberMaterial", _amberMaterial);
	file->Serialize(data, "_greenMaterial", _greenMaterial);
	file->Serialize(data, "_walkMesh", _walkMesh);
	file->Serialize(data, "_dontWalkMesh", _dontWalkMesh);
	file->Serialize(data, "_walkMaterial", _walkMaterial);
	file->Serialize(data, "_dontWalkMaterial", _dontWalkMaterial);
}

void TrafficSignalComponent::Deserialize(json& data, HexEngine::JsonFile* file, uint32_t mask)
{
	(void)mask;
	file->Deserialize(data, "_controllerEntityName", _controllerEntityName);
	file->Deserialize(data, "_arm", _arm);
	file->Deserialize(data, "_role", _role);
	file->Deserialize(data, "_redMaterial", _redMaterial);
	file->Deserialize(data, "_amberMaterial", _amberMaterial);
	file->Deserialize(data, "_greenMaterial", _greenMaterial);
	file->Deserialize(data, "_walkMesh", _walkMesh);
	file->Deserialize(data, "_dontWalkMesh", _dontWalkMesh);
	file->Deserialize(data, "_walkMaterial", _walkMaterial);
	file->Deserialize(data, "_dontWalkMaterial", _dontWalkMaterial);
	_appliedState = -1;
}

bool TrafficSignalComponent::CreateWidget(HexEngine::ComponentWidget* widget)
{
	const int32_t w = widget->GetSize().x - 20;
	auto textRow = [&](const wchar_t* label, std::string* value)
	{
		auto* edit = new HexEngine::LineEdit(widget, widget->GetNextPos(), HexEngine::Point(w, 18), label);
		edit->SetValue(std::wstring(value->begin(), value->end()));
		edit->SetDoesCallbackWaitForReturn(false);
		edit->SetOnInputFn([this, value](HexEngine::LineEdit*, const std::wstring& text)
			{
				*value = ws2s(text);
				_appliedState = -1;
			});
	};

	textRow(L"Controller Entity (empty = parent's)", &_controllerEntityName);
	textRow(L"Arm (N/E/S/W)", &_arm);
	new HexEngine::DragInt(widget, widget->GetNextPos(), HexEngine::Point(w, 18), L"Role (0 vehicle, 1 pedestrian)", &_role, 0, 1, 1);
	textRow(L"Red Material", &_redMaterial);
	textRow(L"Amber Material", &_amberMaterial);
	textRow(L"Green Material", &_greenMaterial);
	textRow(L"Walk Mesh", &_walkMesh);
	textRow(L"Don't Walk Mesh", &_dontWalkMesh);
	textRow(L"Walk Material", &_walkMaterial);
	textRow(L"Don't Walk Material", &_dontWalkMaterial);
	return true;
}
