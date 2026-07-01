

#include "NavMeshLinkComponent.hpp"
#include "../Entity.hpp"
#include "../../HexEngine.hpp"
#include "../../Graphics/DebugRenderer.hpp"
#include "../../GUI/Elements/ComponentWidget.hpp"
#include "../../GUI/Elements/Vector3Edit.hpp"
#include "../../GUI/Elements/DragFloat.hpp"
#include "../../GUI/Elements/Checkbox.hpp"

#include <algorithm>

namespace HexEngine
{
	NavMeshLinkComponent::NavMeshLinkComponent(Entity* entity) :
		BaseComponent(entity)
	{
	}

	NavMeshLinkComponent::NavMeshLinkComponent(Entity* entity, NavMeshLinkComponent* copy) :
		BaseComponent(entity)
	{
		if (copy == nullptr)
			return;

		_startOffset   = copy->_startOffset;
		_endOffset     = copy->_endOffset;
		_radius        = copy->_radius;
		_bidirectional = copy->_bidirectional;
	}

	void NavMeshLinkComponent::Serialize(json& data, JsonFile* file)
	{
		SERIALIZE_VALUE(_startOffset);
		SERIALIZE_VALUE(_endOffset);
		SERIALIZE_VALUE(_radius);
		SERIALIZE_VALUE(_bidirectional);
	}

	void NavMeshLinkComponent::Deserialize(json& data, JsonFile* file, uint32_t mask)
	{
		(void)mask;
		_serializationState = BaseComponent::SerializationState::Deserializing;

		DESERIALIZE_VALUE(_startOffset);
		DESERIALIZE_VALUE(_endOffset);
		DESERIALIZE_VALUE(_radius);
		DESERIALIZE_VALUE(_bidirectional);

		_serializationState = BaseComponent::SerializationState::Ready;
	}

	void NavMeshLinkComponent::GetWorldLink(NavMeshLink& out) const
	{
		Entity* ent = GetEntity();
		const math::Matrix world = ent ? ent->GetWorldTM() : math::Matrix::Identity;

		out.start = math::Vector3::Transform(_startOffset, world);
		out.end   = math::Vector3::Transform(_endOffset, world);
		out.radius = std::max(0.01f, _radius);
		out.bidirectional = _bidirectional;
	}

	bool NavMeshLinkComponent::CreateWidget(ComponentWidget* widget)
	{
		const int32_t fullWidth = widget->GetSize().x - 20;

		new Vector3Edit(widget, widget->GetNextPos(), Point(fullWidth, 18), L"Start Offset", &_startOffset,
			[this](const math::Vector3& v) { _startOffset = v; });
		new Vector3Edit(widget, widget->GetNextPos(), Point(fullWidth, 18), L"End Offset", &_endOffset,
			[this](const math::Vector3& v) { _endOffset = v; });

		new DragFloat(widget, widget->GetNextPos(), Point(fullWidth, 18), L"Connect Radius",
			&_radius, 0.05f, 50.0f, 0.05f, 2);

		new Checkbox(widget, widget->GetNextPos(), Point(fullWidth, 18), L"Bidirectional", &_bidirectional);

		return true;
	}

	void NavMeshLinkComponent::OnRenderEditorGizmo(bool isSelected, bool& /*isHovering*/)
	{
		Entity* ent = GetEntity();
		if (ent == nullptr || g_pEnv == nullptr || g_pEnv->_debugRenderer == nullptr)
			return;

		const math::Matrix world = ent->GetWorldTM();
		const math::Vector3 start = math::Vector3::Transform(_startOffset, world);
		const math::Vector3 end   = math::Vector3::Transform(_endOffset, world);

		const float a = isSelected ? 0.95f : 0.55f;
		const math::Color startColour(0.2f, 0.6f, 1.0f, a); // blue = from
		const math::Color endColour(0.3f, 1.0f, 0.5f, a);   // green = to
		const math::Color lineColour(0.4f, 0.9f, 1.0f, a);

		// Endpoint markers.
		const float m = std::max(0.08f, _radius * 0.5f);
		auto drawMarker = [&](const math::Vector3& p, const math::Color& c)
		{
			dx::BoundingBox box;
			box.Center = p;
			box.Extents = math::Vector3(m, m, m);
			g_pEnv->_debugRenderer->DrawAABB(box, c);
		};
		drawMarker(start, startColour);
		drawMarker(end, endColour);

		// Connection line.
		g_pEnv->_debugRenderer->DrawLine(start, end, lineColour);

		// Direction arrowhead(s): always start->end; add the reverse barb when bidir.
		math::Vector3 dir = end - start;
		if (dir.LengthSquared() > 1e-6f)
		{
			dir.Normalize();
			math::Vector3 up = math::Vector3::Up;
			math::Vector3 side = dir.Cross(up);
			if (side.LengthSquared() < 1e-4f)
				side = dir.Cross(math::Vector3::Right);
			if (side.LengthSquared() > 0.0f)
				side.Normalize();

			const float head = std::max(0.15f, _radius);
			auto drawArrow = [&](const math::Vector3& tip, const math::Vector3& along, const math::Color& c)
			{
				g_pEnv->_debugRenderer->DrawLine(tip, tip - along * head + side * head * 0.5f, c);
				g_pEnv->_debugRenderer->DrawLine(tip, tip - along * head - side * head * 0.5f, c);
			};
			drawArrow(end, dir, lineColour);
			if (_bidirectional)
				drawArrow(start, -dir, lineColour);
		}
	}
}
