#pragma once

#include <HexEngine.Core/HexEngine.hpp>
#include "WeatherTypes.hpp"

namespace HexEngine::Weather
{
	class WeatherZoneComponent final : public UpdateComponent
	{
	public:
		CREATE_COMPONENT_ID(WeatherZoneComponent);
		DEFINE_COMPONENT_CTOR(WeatherZoneComponent);

		virtual void Update(float frameTime) override;
		virtual void Serialize(json& data, JsonFile* file) override;
		virtual void Deserialize(json& data, JsonFile* file, uint32_t mask = 0) override;
		virtual bool CreateWidget(ComponentWidget* widget) override;
		virtual void OnRenderEditorGizmo(bool isSelected, bool& isHovering) override;

		float EvaluateInfluence(const math::Vector3& worldPosition) const;
		WeatherState ResolveState() const;

		int32_t GetPriority() const { return _priority; }
		bool IsEnabled() const { return _enabled; }

	private:
		void ApplyPreset(WeatherPresetId presetId);

	private:
		bool _enabled = true;
		WeatherZoneShape _shape = WeatherZoneShape::Sphere;
		float _sphereRadius = 25.0f;
		math::Vector3 _boxExtents = math::Vector3(25.0f, 10.0f, 25.0f);
		float _blendDistance = 16.0f;
		int32_t _priority = 0;
		WeatherPresetId _presetId = WeatherPresetId::Clear;
		WeatherState _customState = MakePresetState(WeatherPresetId::Clear);
	};
}
