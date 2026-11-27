#include "TerrainCutout.hpp"
#include "../Entity.hpp"
#include "../../HexEngine.hpp"
#include "../../Graphics/DebugRenderer.hpp"
#include "../../GUI/Elements/ComponentWidget.hpp"
#include "../../GUI/Elements/Vector3Edit.hpp"
#include "../../GUI/Elements/Checkbox.hpp"

#include <atomic>
#include <cmath>

namespace HexEngine
{
	namespace
	{
		std::atomic<uint64_t> g_terrainCutoutRevision{ 1 };
	}

	uint64_t GetTerrainCutoutRevision() { return g_terrainCutoutRevision.load(); }
	void BumpTerrainCutoutRevision() { g_terrainCutoutRevision.fetch_add(1); }

	TerrainCutout::TerrainCutout(Entity* entity) :
		BaseComponent(entity)
	{
		BumpTerrainCutoutRevision();
	}

	TerrainCutout::TerrainCutout(Entity* entity, TerrainCutout* copy) :
		BaseComponent(entity)
	{
		if (copy != nullptr)
		{
			_centerOffset = copy->_centerOffset;
			_halfExtents = copy->_halfExtents;
			_enabled = copy->_enabled;
		}
		BumpTerrainCutoutRevision();
	}

	void TerrainCutout::Serialize(json& data, JsonFile* file)
	{
		SERIALIZE_VALUE(_centerOffset);
		SERIALIZE_VALUE(_halfExtents);
		SERIALIZE_VALUE(_enabled);
	}

	void TerrainCutout::Deserialize(json& data, JsonFile* file, uint32_t mask)
	{
		(void)mask;
		_serializationState = BaseComponent::SerializationState::Deserializing;
		DESERIALIZE_VALUE(_centerOffset);
		DESERIALIZE_VALUE(_halfExtents);
		DESERIALIZE_VALUE(_enabled);
		_serializationState = BaseComponent::SerializationState::Ready;
		BumpTerrainCutoutRevision();
	}

	bool TerrainCutout::GetWorldBox(TerrainCutoutBox& out) const
	{
		Entity* ent = GetEntity();
		if (!_enabled || ent == nullptr)
			return false;

		const math::Matrix& world = const_cast<Entity*>(ent)->GetWorldTM();
		const math::Vector3 rowX(world._11, world._12, world._13);
		const math::Vector3 rowY(world._21, world._22, world._23);
		const math::Vector3 rowZ(world._31, world._32, world._33);
		const float sx = rowX.Length(), sy = rowY.Length(), sz = rowZ.Length();

		out.centre = math::Vector3::Transform(_centerOffset, world);
		out.halfExtents = math::Vector3(std::fabs(_halfExtents.x) * sx, std::fabs(_halfExtents.y) * sy, std::fabs(_halfExtents.z) * sz);

		// Upright box: keep the yaw of the entity's X axis, drop any pitch/roll.
		math::Vector2 ax(rowX.x, rowX.z);
		const float len = ax.Length();
		out.axisX = len > 1e-5f ? ax / len : math::Vector2(1.0f, 0.0f);
		return out.halfExtents.x > 0.0f && out.halfExtents.y > 0.0f && out.halfExtents.z > 0.0f;
	}

	bool TerrainCutout::CreateWidget(ComponentWidget* widget)
	{
		const int32_t fullWidth = widget->GetSize().x - 20;
		new Vector3Edit(widget, widget->GetNextPos(), Point(fullWidth, 18), L"Center Offset", &_centerOffset,
			[this](const math::Vector3& v) { _centerOffset = v; BumpTerrainCutoutRevision(); });
		new Vector3Edit(widget, widget->GetNextPos(), Point(fullWidth, 18), L"Half Extents", &_halfExtents,
			[this](const math::Vector3& v) { _halfExtents = v; BumpTerrainCutoutRevision(); });
		auto* enabled = new Checkbox(widget, widget->GetNextPos(), Point(fullWidth, 18), L"Enabled", &_enabled);
		enabled->SetOnCheckFn([](Checkbox*, bool) { BumpTerrainCutoutRevision(); });
		return true;
	}

	void TerrainCutout::OnRenderEditorGizmo(bool isSelected, bool& /*isHovering*/)
	{
		TerrainCutoutBox box;
		if (!GetWorldBox(box) || g_pEnv->_debugRenderer == nullptr)
			return;

		dx::BoundingOrientedBox obb;
		obb.Center = box.centre;
		obb.Extents = dx::XMFLOAT3(box.halfExtents.x, box.halfExtents.y, box.halfExtents.z);
		obb.Orientation = math::Quaternion::CreateFromYawPitchRoll(std::atan2(-box.axisX.y, box.axisX.x), 0.0f, 0.0f);
		g_pEnv->_debugRenderer->DrawOBB(obb, math::Color(0.95f, 0.6f, 0.1f, isSelected ? 0.95f : 0.45f));
	}
}
