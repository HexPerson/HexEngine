

#include "NavMeshBlockingVolume.hpp"
#include "../Entity.hpp"
#include "../../HexEngine.hpp"
#include "../../Graphics/DebugRenderer.hpp"
#include "../../GUI/Elements/ComponentWidget.hpp"
#include "../../GUI/Elements/Vector3Edit.hpp"
#include "../../GUI/Elements/Checkbox.hpp"

#include <algorithm>
#include <limits>

namespace HexEngine
{
	NavMeshBlockingVolume::NavMeshBlockingVolume(Entity* entity) :
		BaseComponent(entity)
	{
	}

	NavMeshBlockingVolume::NavMeshBlockingVolume(Entity* entity, NavMeshBlockingVolume* copy) :
		BaseComponent(entity)
	{
		if (copy == nullptr)
			return;

		_centerOffset = copy->_centerOffset;
		_halfExtents  = copy->_halfExtents;
		_mode         = copy->_mode;
	}

	void NavMeshBlockingVolume::Serialize(json& data, JsonFile* file)
	{
		SERIALIZE_VALUE(_centerOffset);
		SERIALIZE_VALUE(_halfExtents);

		int32_t mode = (int32_t)_mode;
		file->Serialize(data, "_mode", mode);
	}

	void NavMeshBlockingVolume::Deserialize(json& data, JsonFile* file, uint32_t mask)
	{
		(void)mask;
		_serializationState = BaseComponent::SerializationState::Deserializing;

		DESERIALIZE_VALUE(_centerOffset);
		DESERIALIZE_VALUE(_halfExtents);

		int32_t mode = (int32_t)_mode;
		file->Deserialize(data, "_mode", mode);
		_mode = (Mode)mode;

		_serializationState = BaseComponent::SerializationState::Ready;
	}

	void NavMeshBlockingVolume::GetWorldFootprint(NavMeshBlockingBox& out) const
	{
		out.mode = (int32_t)_mode;

		Entity* ent = GetEntity();
		const math::Matrix world = ent ? ent->GetWorldTM() : math::Matrix::Identity;

		const math::Vector3 c = _centerOffset;
		const math::Vector3 e = _halfExtents;

		// Indices 0-3 are the bottom face in winding order (forms the convex XZ
		// footprint); all 8 corners feed the vertical range so box rotation/scale is
		// respected.
		const math::Vector3 local[8] =
		{
			c + math::Vector3(-e.x, -e.y, -e.z),
			c + math::Vector3( e.x, -e.y, -e.z),
			c + math::Vector3( e.x, -e.y,  e.z),
			c + math::Vector3(-e.x, -e.y,  e.z),
			c + math::Vector3(-e.x,  e.y, -e.z),
			c + math::Vector3( e.x,  e.y, -e.z),
			c + math::Vector3( e.x,  e.y,  e.z),
			c + math::Vector3(-e.x,  e.y,  e.z),
		};

		float ymin =  std::numeric_limits<float>::max();
		float ymax = -std::numeric_limits<float>::max();

		for (int32_t i = 0; i < 8; ++i)
		{
			const math::Vector3 w = math::Vector3::Transform(local[i], world);
			ymin = std::min(ymin, w.y);
			ymax = std::max(ymax, w.y);
			if (i < 4)
				out.corners[i] = w;
		}

		out.ymin = ymin;
		out.ymax = ymax;
	}

	bool NavMeshBlockingVolume::CreateWidget(ComponentWidget* widget)
	{
		const int32_t fullWidth = widget->GetSize().x - 20;

		new Vector3Edit(widget, widget->GetNextPos(), Point(fullWidth, 18), L"Center Offset", &_centerOffset,
			[this](const math::Vector3& v) { _centerOffset = v; });
		new Vector3Edit(widget, widget->GetNextPos(), Point(fullWidth, 18), L"Half Extents", &_halfExtents,
			[this](const math::Vector3& v) { _halfExtents = v; });

		// Block (unchecked) vs ForceWalkable (checked). Crossing prefabs tick this so
		// they carve walkability back over an overlapping road block. Bound to a
		// bool* (the Checkbox callback form is display-only); _mode is the source of
		// truth and is kept in sync via the check callback.
		_forceWalkableUI = (_mode == Mode::ForceWalkable);
		auto* modeBox = new Checkbox(widget, widget->GetNextPos(), Point(fullWidth, 18),
			L"Force walkable (crossing)", &_forceWalkableUI);
		modeBox->SetOnCheckFn([this](Checkbox*, bool value) { _mode = value ? Mode::ForceWalkable : Mode::Block; });

		return true;
	}

	void NavMeshBlockingVolume::OnRenderEditorGizmo(bool isSelected, bool& /*isHovering*/)
	{
		Entity* ent = GetEntity();
		if (ent == nullptr)
			return;

		dx::BoundingOrientedBox localBox;
		localBox.Center      = _centerOffset;
		localBox.Extents     = dx::XMFLOAT3(std::max(0.01f, _halfExtents.x), std::max(0.01f, _halfExtents.y), std::max(0.01f, _halfExtents.z));
		localBox.Orientation = math::Quaternion::Identity;

		dx::BoundingOrientedBox worldBox;
		localBox.Transform(worldBox, ent->GetWorldTM());

		const bool block = (_mode == Mode::Block);
		const float a = isSelected ? 0.9f : 0.45f;
		const math::Color colour = block
			? math::Color(1.0f, 0.25f, 0.2f, a)
			: math::Color(0.25f, 1.0f, 0.45f, a);

		g_pEnv->_debugRenderer->DrawOBB(worldBox, colour);
	}
}
