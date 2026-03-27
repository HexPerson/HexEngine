#pragma once

#include "UpdateComponent.hpp"

namespace HexEngine
{
	class HEX_API DayNightCycleComponent : public UpdateComponent
	{
	public:
		CREATE_COMPONENT_ID(DayNightCycleComponent);
		DEFINE_COMPONENT_CTOR(DayNightCycleComponent);

		virtual void Update(float frameTime) override;

		virtual void Serialize(json& data, JsonFile* file) override;
		virtual void Deserialize(json& data, JsonFile* file, uint32_t mask = 0) override;
		virtual bool CreateWidget(ComponentWidget* widget) override;

	private:
		void AdvanceTime(float frameTime);
		void ApplySunRotation();
		void ApplyAmbientLight();
		float ComputeDaylightFactor(float hour) const;
		void NormalizeTimelineSettings();

		void SetCurrentTimeHours(float value);
		void SetDayLengthSeconds(float value);
		void SetNightLengthSeconds(float value);
		void SetHoursPerDay(float value);
		void SetSunriseStartHour(float value);
		void SetDayStartHour(float value);
		void SetNightStartHour(float value);
		void SetSunsetEndHour(float value);
		void SetMinSegmentLength(float value);
		void SetSunYawDegrees(float value);

	private:
		bool _simulateCycle = true;
		float _dayLengthSeconds = 600.0f;
		float _nightLengthSeconds = 480.0f;
		float _currentTimeHours = 12.0f;
		float _hoursPerDay = 24.0f;
		float _sunriseStartHour = 5.0f;
		float _dayStartHour = 6.0f;
		float _nightStartHour = 18.0f;
		float _sunsetEndHour = 19.0f;
		float _minSegmentLength = 1.0f;
		float _sunYawDegrees = 0.0f;
		math::Color _dayAmbientLight = math::Color(0.14f, 0.14f, 0.145f, 1.0f);
		math::Color _nightAmbientLight = math::Color(9.0f / 255.0f, 14.0f / 255.0f, 44.0f / 255.0f, 1.0f);
	};
}
