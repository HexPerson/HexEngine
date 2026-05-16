#include "WeatherZoneComponent.hpp"

#include <algorithm>
#include <cmath>

namespace HexEngine::Weather
{
	namespace
	{
		void PopulatePresetDropDown(DropDown* dropDown, WeatherPresetId currentPreset, std::function<void(WeatherPresetId)> onPick)
		{
			if (dropDown == nullptr)
				return;

			dropDown->SetValue(GetPresetDisplayName(currentPreset));

			for (int32_t i = static_cast<int32_t>(WeatherPresetId::Custom); i <= static_cast<int32_t>(WeatherPresetId::Sandstorm); ++i)
			{
				const WeatherPresetId presetId = static_cast<WeatherPresetId>(i);
				dropDown->GetContextMenu()->AddItem(new ContextItem(
					GetPresetDisplayName(presetId),
					[dropDown, onPick, presetId](const std::wstring&)
					{
						dropDown->SetValue(GetPresetDisplayName(presetId));
						onPick(presetId);
					}));
			}
		}
	}

	WeatherZoneComponent::WeatherZoneComponent(Entity* entity) :
		UpdateComponent(entity)
	{
	}

	WeatherZoneComponent::WeatherZoneComponent(Entity* entity, WeatherZoneComponent* copy) :
		UpdateComponent(entity, copy)
	{
		if (copy != nullptr)
		{
			_enabled = copy->_enabled;
			_shape = copy->_shape;
			_sphereRadius = copy->_sphereRadius;
			_boxExtents = copy->_boxExtents;
			_blendDistance = copy->_blendDistance;
			_priority = copy->_priority;
			_presetId = copy->_presetId;
			_customState = copy->_customState;
		}
	}

	void WeatherZoneComponent::Update(float frameTime)
	{
		UpdateComponent::Update(frameTime);
		(void)frameTime;
	}

	void WeatherZoneComponent::Serialize(json& data, JsonFile* file)
	{
		SERIALIZE_VALUE(_enabled);
		int32_t shape = static_cast<int32_t>(_shape);
		file->Serialize(data, "_shape", shape);
		SERIALIZE_VALUE(_sphereRadius);
		SERIALIZE_VALUE(_boxExtents);
		SERIALIZE_VALUE(_blendDistance);
		SERIALIZE_VALUE(_priority);
		int32_t presetId = static_cast<int32_t>(_presetId);
		file->Serialize(data, "_presetId", presetId);
		SerializeWeatherState(_customState, data["_customState"], file);
	}

	void WeatherZoneComponent::Deserialize(json& data, JsonFile* file, uint32_t mask)
	{
		(void)mask;
		DESERIALIZE_VALUE(_enabled);
		int32_t shape = static_cast<int32_t>(_shape);
		file->Deserialize(data, "_shape", shape);
		_shape = static_cast<WeatherZoneShape>(shape);
		DESERIALIZE_VALUE(_sphereRadius);
		DESERIALIZE_VALUE(_boxExtents);
		DESERIALIZE_VALUE(_blendDistance);
		DESERIALIZE_VALUE(_priority);
		int32_t presetId = static_cast<int32_t>(_presetId);
		file->Deserialize(data, "_presetId", presetId);
		_presetId = static_cast<WeatherPresetId>(presetId);
		if (data.contains("_customState"))
			DeserializeWeatherState(_customState, data["_customState"], file);
	}

	bool WeatherZoneComponent::CreateWidget(ComponentWidget* widget)
	{
		auto* enabled = new Checkbox(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Enabled", &_enabled);
		enabled->SetPrefabOverrideBinding(GetComponentName(), "/_enabled");

		auto* preset = new DropDown(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Preset");
		PopulatePresetDropDown(preset, _presetId, [this](WeatherPresetId presetId)
		{
			ApplyPreset(presetId);
		});

		auto* shape = new DropDown(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Shape");
		shape->SetValue(GetZoneShapeDisplayName(_shape));
		shape->GetContextMenu()->AddItem(new ContextItem(L"Sphere", [this, shape](const std::wstring&)
		{
			_shape = WeatherZoneShape::Sphere;
			shape->SetValue(GetZoneShapeDisplayName(_shape));
		}));
		shape->GetContextMenu()->AddItem(new ContextItem(L"Box", [this, shape](const std::wstring&)
		{
			_shape = WeatherZoneShape::Box;
			shape->SetValue(GetZoneShapeDisplayName(_shape));
		}));

		new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Sphere Radius", &_sphereRadius, 0.1f, 5000.0f, 0.1f, 2);
		new Vector3Edit(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Box Extents", &_boxExtents, [this](const math::Vector3& value) { _boxExtents = value; });
		new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Blend Distance", &_blendDistance, 0.0f, 5000.0f, 0.1f, 2);
		new DragInt(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Priority", &_priority, -100, 100, 1);
		new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Precip Intensity", &_customState.precipitationIntensity, 0.0f, 1.0f, 0.01f, 3);
		new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Wetness", &_customState.surface.wetness, 0.0f, 1.0f, 0.01f, 3);
		new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Puddles", &_customState.surface.puddleAmount, 0.0f, 1.0f, 0.01f, 3);
		new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Snow Coverage", &_customState.surface.snowCoverage, 0.0f, 1.0f, 0.01f, 3);

		return true;
	}

	void WeatherZoneComponent::OnRenderEditorGizmo(bool isSelected, bool& isHovering)
	{
		(void)isHovering;
		if (!isSelected || g_pEnv == nullptr || g_pEnv->_debugRenderer == nullptr)
			return;

		const math::Vector3 center = GetEntity()->GetWorldTM().Translation();
		if (_shape == WeatherZoneShape::Sphere)
		{
			g_pEnv->_debugRenderer->DrawAABB(
				dx::BoundingBox(center, math::Vector3(_sphereRadius, _sphereRadius, _sphereRadius)),
				math::Color(0.2f, 0.6f, 1.0f, 0.8f));
		}
		else
		{
			g_pEnv->_debugRenderer->DrawAABB(
				dx::BoundingBox(center, _boxExtents),
				math::Color(0.2f, 0.6f, 1.0f, 0.8f));
		}
	}

	float WeatherZoneComponent::EvaluateInfluence(const math::Vector3& worldPosition) const
	{
		if (!_enabled)
			return 0.0f;

		const math::Vector3 center = GetEntity()->GetWorldTM().Translation();
		float outsideDistance = 0.0f;

		if (_shape == WeatherZoneShape::Sphere)
		{
			outsideDistance = std::max(0.0f, (worldPosition - center).Length() - std::max(0.0f, _sphereRadius));
		}
		else
		{
			const math::Vector3 delta = math::Vector3(
				std::max(0.0f, fabsf(worldPosition.x - center.x) - _boxExtents.x),
				std::max(0.0f, fabsf(worldPosition.y - center.y) - _boxExtents.y),
				std::max(0.0f, fabsf(worldPosition.z - center.z) - _boxExtents.z));
			outsideDistance = delta.Length();
		}

		if (_blendDistance <= 0.0f)
			return outsideDistance <= 0.0f ? 1.0f : 0.0f;

		return std::clamp(1.0f - (outsideDistance / _blendDistance), 0.0f, 1.0f);
	}

	WeatherState WeatherZoneComponent::ResolveState() const
	{
		if (_presetId == WeatherPresetId::Custom)
			return _customState;

		WeatherState state = MakePresetState(_presetId);
		state.surface = _customState.surface;
		if (_customState.precipitationIntensity > 0.0f)
			state.precipitationIntensity = _customState.precipitationIntensity;
		return state;
	}

	void WeatherZoneComponent::ApplyPreset(WeatherPresetId presetId)
	{
		_presetId = presetId;
		_customState = MakePresetState(presetId);
	}
}
