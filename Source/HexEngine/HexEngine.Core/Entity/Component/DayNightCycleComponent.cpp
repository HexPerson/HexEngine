#include "DayNightCycleComponent.hpp"

#include "DirectionalLight.hpp"
#include "Transform.hpp"
#include "../../HexEngine.hpp"
#include "../../GUI/Elements/Checkbox.hpp"
#include "../../GUI/Elements/ComponentWidget.hpp"
#include "../../GUI/Elements/ColourPicker.hpp"
#include "../../GUI/Elements/DragFloat.hpp"

namespace HexEngine
{
	DayNightCycleComponent::DayNightCycleComponent(Entity* entity) :
		UpdateComponent(entity)
	{
		NormalizeTimelineSettings();
		ApplySunRotation();
		ApplyAmbientLight();
	}

	DayNightCycleComponent::DayNightCycleComponent(Entity* entity, DayNightCycleComponent* copy) :
		UpdateComponent(entity, copy)
	{
		_dayLengthSeconds = copy->_dayLengthSeconds;
		_nightLengthSeconds = copy->_nightLengthSeconds;
		_currentTimeHours = copy->_currentTimeHours;
		_hoursPerDay = copy->_hoursPerDay;
		_sunriseStartHour = copy->_sunriseStartHour;
		_dayStartHour = copy->_dayStartHour;
		_nightStartHour = copy->_nightStartHour;
		_sunsetEndHour = copy->_sunsetEndHour;
		_minSegmentLength = copy->_minSegmentLength;
		_simulateCycle = copy->_simulateCycle;
		_sunYawDegrees = copy->_sunYawDegrees;
		_dayAmbientLight = copy->_dayAmbientLight;
		_nightAmbientLight = copy->_nightAmbientLight;

		NormalizeTimelineSettings();
		ApplySunRotation();
		ApplyAmbientLight();
	}

	void DayNightCycleComponent::Update(float frameTime)
	{
		UpdateComponent::Update(frameTime);

		if (GetEntity()->GetComponent<DirectionalLight>() == nullptr)
			return;

		if (_simulateCycle)
			AdvanceTime(frameTime);

		ApplySunRotation();
		ApplyAmbientLight();
	}

	void DayNightCycleComponent::Serialize(json& data, JsonFile* file)
	{
		SERIALIZE_VALUE(_simulateCycle);
		SERIALIZE_VALUE(_dayLengthSeconds);
		SERIALIZE_VALUE(_nightLengthSeconds);
		SERIALIZE_VALUE(_currentTimeHours);
		SERIALIZE_VALUE(_hoursPerDay);
		SERIALIZE_VALUE(_sunriseStartHour);
		SERIALIZE_VALUE(_dayStartHour);
		SERIALIZE_VALUE(_nightStartHour);
		SERIALIZE_VALUE(_sunsetEndHour);
		SERIALIZE_VALUE(_minSegmentLength);
		SERIALIZE_VALUE(_sunYawDegrees);
		SERIALIZE_VALUE(_dayAmbientLight);
		SERIALIZE_VALUE(_nightAmbientLight);
	}

	void DayNightCycleComponent::Deserialize(json& data, JsonFile* file, uint32_t mask)
	{
		(void)mask;

		DESERIALIZE_VALUE(_simulateCycle);
		DESERIALIZE_VALUE(_dayLengthSeconds);
		DESERIALIZE_VALUE(_nightLengthSeconds);
		DESERIALIZE_VALUE(_currentTimeHours);
		DESERIALIZE_VALUE(_hoursPerDay);
		DESERIALIZE_VALUE(_sunriseStartHour);
		DESERIALIZE_VALUE(_dayStartHour);
		DESERIALIZE_VALUE(_nightStartHour);
		DESERIALIZE_VALUE(_sunsetEndHour);
		DESERIALIZE_VALUE(_minSegmentLength);
		DESERIALIZE_VALUE(_sunYawDegrees);
		DESERIALIZE_VALUE(_dayAmbientLight);
		DESERIALIZE_VALUE(_nightAmbientLight);

		SetHoursPerDay(_hoursPerDay);
		SetSunriseStartHour(_sunriseStartHour);
		SetDayStartHour(_dayStartHour);
		SetNightStartHour(_nightStartHour);
		SetSunsetEndHour(_sunsetEndHour);
		SetMinSegmentLength(_minSegmentLength);
		SetSunYawDegrees(_sunYawDegrees);
		SetDayLengthSeconds(_dayLengthSeconds);
		SetNightLengthSeconds(_nightLengthSeconds);
		SetCurrentTimeHours(_currentTimeHours);
		ApplySunRotation();
		ApplyAmbientLight();
	}

	bool DayNightCycleComponent::CreateWidget(ComponentWidget* widget)
	{
		auto* runCycle = new Checkbox(
			widget,
			widget->GetNextPos(),
			Point(widget->GetSize().x - 20, 18),
			L"Run Cycle",
			&_simulateCycle);
		runCycle->SetPrefabOverrideBinding(GetComponentName(), "/_simulateCycle");

		auto* dayLength = new DragFloat(
			widget,
			widget->GetNextPos(),
			Point(widget->GetSize().x - 140, 18),
			L"Day Length (sec)",
			&_dayLengthSeconds,
			0.01f,
			86400.0f,
			1.0f,
			1);
		dayLength->SetPrefabOverrideBinding(GetComponentName(), "/_dayLengthSeconds");
		dayLength->SetOnDrag([this](float value, float, float)
		{
			SetDayLengthSeconds(value);
		});

		auto* nightLength = new DragFloat(
			widget,
			widget->GetNextPos(),
			Point(widget->GetSize().x - 140, 18),
			L"Night Length (sec)",
			&_nightLengthSeconds,
			0.01f,
			86400.0f,
			1.0f,
			1);
		nightLength->SetPrefabOverrideBinding(GetComponentName(), "/_nightLengthSeconds");
		nightLength->SetOnDrag([this](float value, float, float)
		{
			SetNightLengthSeconds(value);
		});

		auto* currentTime = new DragFloat(
			widget,
			widget->GetNextPos(),
			Point(widget->GetSize().x - 140, 18),
			L"Current Time",
			&_currentTimeHours,
			0.0f,
			240.0f,
			0.05f,
			2);
		currentTime->SetPrefabOverrideBinding(GetComponentName(), "/_currentTimeHours");
		currentTime->SetOnDrag([this](float value, float, float)
		{
			SetCurrentTimeHours(value);
			ApplySunRotation();
			ApplyAmbientLight();
		});

		auto* sunYaw = new DragFloat(
			widget,
			widget->GetNextPos(),
			Point(widget->GetSize().x - 140, 18),
			L"Sun Yaw (deg)",
			&_sunYawDegrees,
			-360.0f,
			360.0f,
			0.1f,
			2);
		sunYaw->SetPrefabOverrideBinding(GetComponentName(), "/_sunYawDegrees");
		sunYaw->SetOnDrag([this](float value, float, float)
		{
			SetSunYawDegrees(value);
			ApplySunRotation();
		});

		auto* hoursPerDay = new DragFloat(
			widget,
			widget->GetNextPos(),
			Point(widget->GetSize().x - 140, 18),
			L"Hours Per Day",
			&_hoursPerDay,
			1.0f,
			240.0f,
			0.1f,
			2);
		hoursPerDay->SetPrefabOverrideBinding(GetComponentName(), "/_hoursPerDay");
		hoursPerDay->SetOnDrag([this](float value, float, float)
		{
			SetHoursPerDay(value);
			ApplySunRotation();
			ApplyAmbientLight();
		});

		auto* sunriseStart = new DragFloat(
			widget,
			widget->GetNextPos(),
			Point(widget->GetSize().x - 140, 18),
			L"Sunrise Starts",
			&_sunriseStartHour,
			0.0f,
			240.0f,
			0.05f,
			2);
		sunriseStart->SetPrefabOverrideBinding(GetComponentName(), "/_sunriseStartHour");
		sunriseStart->SetOnDrag([this](float value, float, float)
		{
			SetSunriseStartHour(value);
			ApplyAmbientLight();
		});

		auto* dayStart = new DragFloat(
			widget,
			widget->GetNextPos(),
			Point(widget->GetSize().x - 140, 18),
			L"Day Starts",
			&_dayStartHour,
			0.0f,
			240.0f,
			0.05f,
			2);
		dayStart->SetPrefabOverrideBinding(GetComponentName(), "/_dayStartHour");
		dayStart->SetOnDrag([this](float value, float, float)
		{
			SetDayStartHour(value);
			ApplyAmbientLight();
		});

		auto* nightStart = new DragFloat(
			widget,
			widget->GetNextPos(),
			Point(widget->GetSize().x - 140, 18),
			L"Night Starts",
			&_nightStartHour,
			0.0f,
			240.0f,
			0.05f,
			2);
		nightStart->SetPrefabOverrideBinding(GetComponentName(), "/_nightStartHour");
		nightStart->SetOnDrag([this](float value, float, float)
		{
			SetNightStartHour(value);
			ApplyAmbientLight();
		});

		auto* sunsetEnd = new DragFloat(
			widget,
			widget->GetNextPos(),
			Point(widget->GetSize().x - 140, 18),
			L"Sunset Ends",
			&_sunsetEndHour,
			0.0f,
			240.0f,
			0.05f,
			2);
		sunsetEnd->SetPrefabOverrideBinding(GetComponentName(), "/_sunsetEndHour");
		sunsetEnd->SetOnDrag([this](float value, float, float)
		{
			SetSunsetEndHour(value);
			ApplyAmbientLight();
		});

		auto* minSegLength = new DragFloat(
			widget,
			widget->GetNextPos(),
			Point(widget->GetSize().x - 140, 18),
			L"Min Segment (sec)",
			&_minSegmentLength,
			0.01f,
			120.0f,
			0.1f,
			2);
		minSegLength->SetPrefabOverrideBinding(GetComponentName(), "/_minSegmentLength");
		minSegLength->SetOnDrag([this](float value, float, float)
		{
			SetMinSegmentLength(value);
		});

		auto* dayAmbient = new ColourPicker(
			widget,
			widget->GetNextPos(),
			Point(widget->GetSize().x - 140, 18),
			L"Day Ambient",
			&_dayAmbientLight);
		dayAmbient->SetPrefabOverrideBinding(GetComponentName(), "/_dayAmbientLight");

		auto* nightAmbient = new ColourPicker(
			widget,
			widget->GetNextPos(),
			Point(widget->GetSize().x - 140, 18),
			L"Night Ambient",
			&_nightAmbientLight);
		nightAmbient->SetPrefabOverrideBinding(GetComponentName(), "/_nightAmbientLight");

		return true;
	}

	void DayNightCycleComponent::AdvanceTime(float frameTime)
	{
		const bool isDay = _currentTimeHours >= _dayStartHour && _currentTimeHours < _nightStartHour;
		const float segmentLength = isDay ? _dayLengthSeconds : _nightLengthSeconds;
		const float safeLength = std::max(segmentLength, _minSegmentLength);

		const float daySpanHours = std::max(_nightStartHour - _dayStartHour, 0.001f);
		const float nightSpanHours = std::max(_hoursPerDay - daySpanHours, 0.001f);
		const float hoursPerSecond = (isDay ? daySpanHours : nightSpanHours) / safeLength;
		SetCurrentTimeHours(_currentTimeHours + frameTime * hoursPerSecond);
	}

	void DayNightCycleComponent::ApplySunRotation()
	{
		if (GetEntity()->GetComponent<DirectionalLight>() == nullptr)
			return;

		auto* transform = GetEntity()->GetComponent<Transform>();
		if (transform == nullptr)
			return;

		// Midday is directly above.
		float pitch = (_currentTimeHours / _hoursPerDay) * 360.0f - 270.0f;
		if (pitch > 180.0f)
			pitch -= 360.0f;

		const auto rotation = math::Quaternion::CreateFromYawPitchRoll(ToRadian(_sunYawDegrees), ToRadian(pitch), ToRadian(0.0f));
		transform->SetRotationNoNotify(rotation);
	}

	void DayNightCycleComponent::ApplyAmbientLight()
	{
		auto scene = g_pEnv->_sceneManager->GetCurrentScene();
		if (scene == nullptr)
			return;

		const float daylight = ComputeDaylightFactor(_currentTimeHours);
		math::Color ambient;
		math::Color::Lerp(_nightAmbientLight, _dayAmbientLight, daylight, ambient);

		scene->SetAmbientLight(ambient);

		if (auto light = GetEntity()->GetComponent<DirectionalLight>(); light != nullptr)
		{
			light->SetLightMultiplier(daylight);
		}
	}

	float DayNightCycleComponent::ComputeDaylightFactor(float hour) const
	{
		if (hour >= _dayStartHour && hour < _nightStartHour)
			return 1.0f;

		if (hour >= _sunriseStartHour && hour < _dayStartHour)
			return (hour - _sunriseStartHour) / std::max(_dayStartHour - _sunriseStartHour, 0.001f);

		if (hour >= _nightStartHour && hour < _sunsetEndHour)
			return 1.0f - ((hour - _nightStartHour) / std::max(_sunsetEndHour - _nightStartHour, 0.001f));

		return 0.0f;
	}

	void DayNightCycleComponent::NormalizeTimelineSettings()
	{
		_hoursPerDay = std::max(_hoursPerDay, 1.0f);
		_minSegmentLength = std::max(_minSegmentLength, 0.01f);

		_sunriseStartHour = std::clamp(_sunriseStartHour, 0.0f, _hoursPerDay);
		_dayStartHour = std::clamp(_dayStartHour, 0.0f, _hoursPerDay);
		_nightStartHour = std::clamp(_nightStartHour, 0.0f, _hoursPerDay);
		_sunsetEndHour = std::clamp(_sunsetEndHour, 0.0f, _hoursPerDay);

		if (_dayStartHour > _nightStartHour)
			std::swap(_dayStartHour, _nightStartHour);

		if (_sunriseStartHour > _dayStartHour)
			_sunriseStartHour = _dayStartHour;

		if (_sunsetEndHour < _nightStartHour)
			_sunsetEndHour = _nightStartHour;

		_dayLengthSeconds = std::max(_dayLengthSeconds, _minSegmentLength);
		_nightLengthSeconds = std::max(_nightLengthSeconds, _minSegmentLength);
		SetCurrentTimeHours(_currentTimeHours);
	}

	void DayNightCycleComponent::SetCurrentTimeHours(float value)
	{
		float wrapped = std::fmod(value, _hoursPerDay);
		if (wrapped < 0.0f)
			wrapped += _hoursPerDay;

		_currentTimeHours = wrapped;
	}

	void DayNightCycleComponent::SetDayLengthSeconds(float value)
	{
		_dayLengthSeconds = std::max(value, _minSegmentLength);
	}

	void DayNightCycleComponent::SetNightLengthSeconds(float value)
	{
		_nightLengthSeconds = std::max(value, _minSegmentLength);
	}

	void DayNightCycleComponent::SetHoursPerDay(float value)
	{
		_hoursPerDay = value;
		NormalizeTimelineSettings();
	}

	void DayNightCycleComponent::SetSunriseStartHour(float value)
	{
		_sunriseStartHour = value;
		NormalizeTimelineSettings();
	}

	void DayNightCycleComponent::SetDayStartHour(float value)
	{
		_dayStartHour = value;
		NormalizeTimelineSettings();
	}

	void DayNightCycleComponent::SetNightStartHour(float value)
	{
		_nightStartHour = value;
		NormalizeTimelineSettings();
	}

	void DayNightCycleComponent::SetSunsetEndHour(float value)
	{
		_sunsetEndHour = value;
		NormalizeTimelineSettings();
	}

	void DayNightCycleComponent::SetMinSegmentLength(float value)
	{
		_minSegmentLength = value;
		NormalizeTimelineSettings();
	}

	void DayNightCycleComponent::SetSunYawDegrees(float value)
	{
		_sunYawDegrees = value;
	}
}
