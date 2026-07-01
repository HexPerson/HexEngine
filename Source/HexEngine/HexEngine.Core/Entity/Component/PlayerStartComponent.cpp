

#include "PlayerStartComponent.hpp"
#include "../Entity.hpp"
#include "../../HexEngine.hpp"
#include "../../Graphics/DebugRenderer.hpp"
#include "../../GUI/Elements/ComponentWidget.hpp"
#include "../../GUI/Elements/Checkbox.hpp"

namespace HexEngine
{
	PlayerStartComponent::PlayerStartComponent(Entity* entity) :
		BaseComponent(entity)
	{
	}

	PlayerStartComponent::PlayerStartComponent(Entity* entity, PlayerStartComponent* copy) :
		BaseComponent(entity)
	{
		if (copy != nullptr)
			_primary = copy->_primary;
	}

	void PlayerStartComponent::Serialize(json& data, JsonFile* file)
	{
		SERIALIZE_VALUE(_primary);
	}

	void PlayerStartComponent::Deserialize(json& data, JsonFile* file, uint32_t mask)
	{
		DESERIALIZE_VALUE(_primary);
	}

	bool PlayerStartComponent::CreateWidget(ComponentWidget* widget)
	{
		auto* primary = new Checkbox(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Primary Spawn", &_primary);
		primary->SetPrefabOverrideBinding(GetComponentName(), "/_primary");
		return true;
	}

	void PlayerStartComponent::OnRenderEditorGizmo(bool isSelected, bool& isHovering)
	{
		if (!g_pEnv->IsEditorMode())
			return;

		const math::Matrix worldTM = GetEntity()->GetWorldTM();
		const math::Vector3 origin = worldTM.Translation();
		const math::Vector3 forward = worldTM.Forward();
		const math::Vector3 up = worldTM.Up();
		const math::Vector3 right = worldTM.Right();

		// Gold marker box at the spawn point.
		const math::Color markerColour = isSelected
			? math::Color(1.0f, 0.85f, 0.2f, 0.9f)
			: math::Color(0.95f, 0.78f, 0.1f, 0.55f);
		dx::BoundingBox marker;
		marker.Center = origin + up * 0.9f;        // lift so the box straddles eye height, not the floor
		marker.Extents = math::Vector3(0.35f, 0.9f, 0.35f);
		g_pEnv->_debugRenderer->DrawAABB(marker, markerColour);

		// Forward arrow showing which way the spawned player will face.
		const math::Vector3 tip = origin + forward * 1.6f + up * 0.9f;
		const math::Vector3 base = origin + up * 0.9f;
		const math::Color arrowColour(0.3f, 0.85f, 1.0f, 0.9f);
		g_pEnv->_debugRenderer->DrawLine(base, tip, arrowColour);
		// Two short barbs to make the arrowhead read.
		g_pEnv->_debugRenderer->DrawLine(tip, tip - forward * 0.35f + right * 0.2f, arrowColour);
		g_pEnv->_debugRenderer->DrawLine(tip, tip - forward * 0.35f - right * 0.2f, arrowColour);
	}
}
