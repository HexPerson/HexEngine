#include "SeatComponent.hpp"
#include <HexEngine.Core/Entity/Entity.hpp>
#include <HexEngine.Core/Entity/Component/StaticMeshComponent.hpp>
#include <HexEngine.Core/Graphics/DebugRenderer.hpp>
#include <HexEngine.Core/GUI/Elements/Checkbox.hpp>
#include <HexEngine.Core/GUI/Elements/ComponentWidget.hpp>
#include <HexEngine.Core/GUI/Elements/DragFloat.hpp>
#include <HexEngine.Core/GUI/Elements/DragInt.hpp>
#include <HexEngine.Core/Scene/Scene.hpp>
#include <algorithm>
#include <cmath>

SeatComponent::SeatComponent(HexEngine::Entity* entity) :
	BaseComponent(entity)
{
}

SeatComponent::SeatComponent(HexEngine::Entity* entity, SeatComponent* copy) :
	BaseComponent(entity)
{
	if (copy != nullptr)
	{
		_enabled = copy->_enabled;
		_slotCount = copy->_slotCount;
		_slotSpacing = copy->_slotSpacing;
		_seatPoint = copy->_seatPoint;
		_slotAxis = copy->_slotAxis;
		_facing = copy->_facing;
		_approachDistance = copy->_approachDistance;
	}
}

namespace
{
	math::Vector3 Flatten(math::Vector3 v)
	{
		v.y = 0.0f;
		const float len = v.Length();
		return len > 1e-5f ? v / len : math::Vector3(0.0f, 0.0f, 1.0f);
	}
}

namespace
{
	math::Quaternion WorldRotation(HexEngine::Entity* e)
	{
		math::Vector3 scale, translation;
		math::Quaternion rotation;
		math::Matrix world = e->GetWorldTM();
		world.Decompose(scale, rotation, translation);
		return rotation;
	}

	void UnionMeshBounds(HexEngine::Entity* e, math::Vector3& mn, math::Vector3& mx, bool& any)
	{
		if (e == nullptr || e->IsPendingDeletion())
			return;
		if (e->GetComponent<HexEngine::StaticMeshComponent>() != nullptr)
		{
			const auto& box = e->GetWorldAABB();
			const math::Vector3 c(box.Center.x, box.Center.y, box.Center.z), x(box.Extents.x, box.Extents.y, box.Extents.z);
			mn = any ? math::Vector3::Min(mn, c - x) : c - x;
			mx = any ? math::Vector3::Max(mx, c + x) : c + x;
			any = true;
		}
		for (auto* child : e->GetChildren())
			UnionMeshBounds(child, mn, mx, any);
	}
}

void SeatComponent::RefreshBounds() const
{
	// In play the seats stay put: compute once. In the editor follow edits.
	const bool editor = HexEngine::g_pEnv != nullptr && HexEngine::g_pEnv->IsEditorMode() && !HexEngine::g_pEnv->IsGameRunning();
	if (_boundsValid && !editor)
		return;
	auto* e = GetEntity();
	math::Vector3 mn, mx;
	bool any = false;
	UnionMeshBounds(e, mn, mx, any);
	if (any)
	{
		_boundsFloor = math::Vector3((mn.x + mx.x) * 0.5f, mn.y, (mn.z + mx.z) * 0.5f);
		_boundsExtents = (mx - mn) * 0.5f;
	}
	else
	{
		_boundsFloor = e != nullptr ? e->GetWorldTM().Translation() : math::Vector3::Zero;
		_boundsExtents = math::Vector3(0.3f, 0.5f, 0.3f);
	}
	_boundsValid = true;
}

math::Vector3 SeatComponent::WorldSlotAxis() const
{
	math::Vector3 axis = _slotAxis;
	if (axis.LengthSquared() < 1e-8f)
		axis = math::Vector3(1.0f, 0.0f, 0.0f);
	axis = math::Vector3::Transform(axis, WorldRotation(GetEntity()));
	axis.y = 0.0f;
	if (axis.LengthSquared() < 1e-8f)
		axis = math::Vector3(1.0f, 0.0f, 0.0f);
	axis.Normalize();
	return axis;
}

int32_t SeatComponent::GetSlotCount() const
{
	if (_slotCount > 0)
		return _slotCount;
	if (GetEntity() == nullptr)
		return 0;
	// Auto: as many as fit across the meshes' width along the slot axis.
	RefreshBounds();
	const math::Vector3 axis = WorldSlotAxis();
	const float width = 2.0f * (std::fabs(axis.x) * _boundsExtents.x + std::fabs(axis.z) * _boundsExtents.z);
	return std::clamp((int32_t)std::floor(width / std::max(0.2f, _slotSpacing) + 0.25f), 1, 8);
}

bool SeatComponent::GetSlot(int32_t slot, math::Vector3& outSeat, math::Vector3& outFacing) const
{
	if (slot < 0 || slot >= GetSlotCount() || GetEntity() == nullptr)
		return false;
	const float offset = ((float)slot - 0.5f * (float)(GetSlotCount() - 1)) * _slotSpacing;
	// Offsets are metres in the seat's frame: rotate them, don't scale them (some
	// kit props carry a non-unit import scale).
	const math::Quaternion rotation = WorldRotation(GetEntity());
	math::Vector3 base;
	if (_anchorToBounds)
	{
		RefreshBounds();
		base = _boundsFloor + math::Vector3::Transform(_seatPoint, rotation);
	}
	else
	{
		base = math::Vector3::Transform(_seatPoint, GetEntity()->GetWorldTM());
	}
	outSeat = base + WorldSlotAxis() * offset;
	outFacing = Flatten(math::Vector3::Transform(_facing, rotation));
	return true;
}

math::Vector3 SeatComponent::GetApproachPoint(int32_t slot) const
{
	math::Vector3 seat, facing;
	if (!GetSlot(slot, seat, facing))
		return GetEntity() != nullptr ? GetEntity()->GetWorldTM().Translation() : math::Vector3::Zero;
	return seat + facing * _approachDistance;
}

void SeatComponent::EnsureOccupancy() const
{
	if ((int32_t)_occupants.size() != GetSlotCount())
		_occupants.resize((size_t)GetSlotCount());
}

bool SeatComponent::IsSlotFree(int32_t slot) const
{
	EnsureOccupancy();
	return _enabled && slot >= 0 && slot < GetSlotCount() && _occupants[(size_t)slot].empty();
}

int32_t SeatComponent::FindFreeSlot(const math::Vector3& nearPoint) const
{
	if (!_enabled)
		return -1;
	EnsureOccupancy();
	int32_t best = -1;
	float bestDist = 0.0f;
	for (int32_t i = 0; i < GetSlotCount(); ++i)
	{
		if (!_occupants[(size_t)i].empty())
			continue;
		// Leave a gap next to a stranger on a bench: skip slots touching an occupied one
		// unless the bench is otherwise empty-ish (2-seaters can still be shared).
		const bool leftTaken = i > 0 && !_occupants[(size_t)i - 1].empty();
		const bool rightTaken = i + 1 < GetSlotCount() && !_occupants[(size_t)i + 1].empty();
		if (GetSlotCount() > 2 && (leftTaken || rightTaken))
			continue;
		math::Vector3 seat, facing;
		if (!GetSlot(i, seat, facing))
			continue;
		const float d = (seat - nearPoint).LengthSquared();
		if (best < 0 || d < bestDist)
		{
			best = i;
			bestDist = d;
		}
	}
	return best;
}

bool SeatComponent::Reserve(int32_t slot, const std::string& agentName)
{
	if (!IsSlotFree(slot))
		return false;
	_occupants[(size_t)slot] = agentName;
	return true;
}

void SeatComponent::Release(int32_t slot, const std::string& agentName)
{
	EnsureOccupancy();
	if (slot >= 0 && slot < GetSlotCount() && _occupants[(size_t)slot] == agentName)
		_occupants[(size_t)slot].clear();
}

void SeatComponent::ReleaseAll()
{
	_occupants.clear();
}

void SeatComponent::OnRenderEditorGizmo(bool isSelected, bool& isHovering)
{
	(void)isHovering;
	if (!isSelected || HexEngine::g_pEnv == nullptr || !HexEngine::g_pEnv->IsEditorMode() || HexEngine::g_pEnv->_debugRenderer == nullptr)
		return;
	auto* dr = HexEngine::g_pEnv->_debugRenderer;
	const math::Color seatColour(0.35f, 1.0f, 0.45f, 0.95f);
	const math::Color approachColour(1.0f, 0.8f, 0.2f, 0.95f);
	for (int32_t i = 0; i < GetSlotCount(); ++i)
	{
		math::Vector3 seat, facing;
		if (!GetSlot(i, seat, facing))
			continue;
		const math::Vector3 side(facing.z, 0.0f, -facing.x);
		const math::Vector3 hip = seat + math::Vector3(0.0f, 0.45f, 0.0f);
		// A small seat square at hip height, a post to the floor and the facing arrow.
		dr->DrawLine(hip - side * 0.2f - facing * 0.2f, hip + side * 0.2f - facing * 0.2f, seatColour);
		dr->DrawLine(hip + side * 0.2f - facing * 0.2f, hip + side * 0.2f + facing * 0.2f, seatColour);
		dr->DrawLine(hip + side * 0.2f + facing * 0.2f, hip - side * 0.2f + facing * 0.2f, seatColour);
		dr->DrawLine(hip - side * 0.2f + facing * 0.2f, hip - side * 0.2f - facing * 0.2f, seatColour);
		dr->DrawLine(seat, hip, seatColour);
		dr->DrawLine(hip, hip + facing * 0.5f, seatColour);
		const math::Vector3 approach = GetApproachPoint(i);
		dr->DrawLine(approach, approach + math::Vector3(0.0f, 1.0f, 0.0f), approachColour);
	}
}

void SeatComponent::Serialize(json& data, HexEngine::JsonFile* file)
{
	file->Serialize(data, "_enabled", _enabled);
	file->Serialize(data, "_anchorToBounds", _anchorToBounds);
	file->Serialize(data, "_slotCount", _slotCount);
	file->Serialize(data, "_slotSpacing", _slotSpacing);
	file->Serialize(data, "_seatPoint", _seatPoint);
	file->Serialize(data, "_slotAxis", _slotAxis);
	file->Serialize(data, "_facing", _facing);
	file->Serialize(data, "_approachDistance", _approachDistance);
}

void SeatComponent::Deserialize(json& data, HexEngine::JsonFile* file, uint32_t mask)
{
	(void)mask;
	file->Deserialize(data, "_enabled", _enabled);
	file->Deserialize(data, "_anchorToBounds", _anchorToBounds);
	file->Deserialize(data, "_slotCount", _slotCount);
	file->Deserialize(data, "_slotSpacing", _slotSpacing);
	file->Deserialize(data, "_seatPoint", _seatPoint);
	file->Deserialize(data, "_slotAxis", _slotAxis);
	file->Deserialize(data, "_facing", _facing);
	file->Deserialize(data, "_approachDistance", _approachDistance);
	_occupants.clear();
	_boundsValid = false;
}

bool SeatComponent::CreateWidget(HexEngine::ComponentWidget* widget)
{
	using namespace HexEngine;
	const int32_t w = widget->GetSize().x - 20;
	new Checkbox(widget, widget->GetNextPos(), Point(w, 18), L"Enabled", &_enabled);
	new Checkbox(widget, widget->GetNextPos(), Point(w, 18), L"Seat point relative to mesh bounds", &_anchorToBounds);
	new DragInt(widget, widget->GetNextPos(), Point(w, 18), L"Slots (0 = fit width)", &_slotCount, 0, 12, 1);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Slot Spacing (m)", &_slotSpacing, 0.2f, 3.0f, 0.01f, 2);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Seat Point X", &_seatPoint.x, -20.0f, 20.0f, 0.01f, 2);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Seat Point Y", &_seatPoint.y, -20.0f, 20.0f, 0.01f, 2);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Seat Point Z", &_seatPoint.z, -20.0f, 20.0f, 0.01f, 2);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Slot Axis X", &_slotAxis.x, -1.0f, 1.0f, 0.01f, 2);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Slot Axis Z", &_slotAxis.z, -1.0f, 1.0f, 0.01f, 2);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Facing X", &_facing.x, -1.0f, 1.0f, 0.01f, 2);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Facing Z", &_facing.z, -1.0f, 1.0f, 0.01f, 2);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Approach Distance (m)", &_approachDistance, 0.0f, 2.0f, 0.01f, 2);
	return true;
}
