#pragma once

#include <HexEngine.Core/HexEngine.hpp>

// A place routine NPCs can sit: a bench, chair, stool, sofa or booth. Each SLOT is
// one sitting position. Directions are in the entity's local frame:
//   _seatPoint    where a seated person's root goes for the middle of the seat.
//                 Y = how far above the floor (the sitting clip is authored for a
//                 standard chair; raise it for taller seats so hips sit ON the
//                 cushion). With
//                 _anchorToBounds (default) it is an offset from the floor-level
//                 centre of the seat's meshes (this entity and its children), so a
//                 kit prop whose pivot isn't under the seat still works; otherwise
//                 it is a point in the entity's local space.
//   _slotAxis     the direction slots are laid out along (a bench's length)
//   _facing       the direction a seated person faces (the kit's seats face +Z)
// Slots are spread _slotSpacing apart, centred on the seat point. _slotCount 0 =
// as many as fit across the meshes' width. An NPC walks to a spot
// _approachDistance in front of the slot, turns round and sits back onto it.
//
// Occupancy is runtime-only: the routine system reserves a slot before walking
// over and releases it when the NPC stands up.
class SeatComponent : public HexEngine::BaseComponent
{
public:
	CREATE_COMPONENT_ID(SeatComponent);
	DEFINE_COMPONENT_CTOR(SeatComponent);

	virtual void Serialize(json& data, HexEngine::JsonFile* file) override;
	virtual void Deserialize(json& data, HexEngine::JsonFile* file, uint32_t mask = 0) override;
	virtual bool CreateWidget(HexEngine::ComponentWidget* widget) override;
	virtual void OnRenderEditorGizmo(bool isSelected, bool& isHovering) override;

	bool IsEnabled() const { return _enabled; }
	int32_t GetSlotCount() const;
	float GetApproachDistance() const { return _approachDistance; }
	// How far above the floor a seated person's root goes (_seatPoint.y): raises the
	// sitting pose onto taller seats so the hips don't sink into the cushion.
	float GetSeatLift() const { return _seatPoint.y; }

	// World-space seated root position and (horizontal, unit) facing for a slot.
	bool GetSlot(int32_t slot, math::Vector3& outSeat, math::Vector3& outFacing) const;
	// Where to stand before sitting down (in front of the slot).
	math::Vector3 GetApproachPoint(int32_t slot) const;

	// The free slot nearest `nearPoint`, or -1.
	int32_t FindFreeSlot(const math::Vector3& nearPoint) const;
	bool IsSlotFree(int32_t slot) const;
	bool Reserve(int32_t slot, const std::string& agentName);
	void Release(int32_t slot, const std::string& agentName);
	void ReleaseAll();

private:
	void EnsureOccupancy() const;
	// Floor-level centre and half-size of the seat's meshes, world space.
	void RefreshBounds() const;
	math::Vector3 WorldSlotAxis() const;

	bool _enabled = true;
	bool _anchorToBounds = true;
	int32_t _slotCount = 1;
	float _slotSpacing = 0.6f;
	math::Vector3 _seatPoint = math::Vector3::Zero;
	math::Vector3 _slotAxis = math::Vector3(1.0f, 0.0f, 0.0f);
	math::Vector3 _facing = math::Vector3(0.0f, 0.0f, 1.0f);
	float _approachDistance = 0.45f;

	mutable std::vector<std::string> _occupants;	// runtime, per slot
	mutable bool _boundsValid = false;				// runtime cache (seats don't move in play)
	mutable math::Vector3 _boundsFloor = math::Vector3::Zero;
	mutable math::Vector3 _boundsExtents = math::Vector3::Zero;
};
