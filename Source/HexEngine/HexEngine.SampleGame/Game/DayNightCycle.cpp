

#include "DayNightCycle.hpp"
#include "../../HexEngine.Core/Environment/IEnvironment.hpp"

float lerp(float a, float b, float f)
{
	return (a * (1.0f - f)) + (b * f);
}

HexEngine::Cvar env_simulateTime("env_simulateTime", "Whether or not to simulate the day/night cycle", true, false, true);
HexEngine::Cvar env_time("env_time", "The current time of day", 0.0f, 0.0f, 1.0f);
HexEngine::Cvar env_sunDistance("env_sunDistance", "The distance of the sun from the world centre", 100.0f, 5000.0f, 2500.0f);

namespace CityBuilder
{
	const float DayCycleSpeed = 0.10f / 100.0f;
	
	math::Color g_dayColour = math::Color(0.24f, 0.24f, 0.25f);

	math::Color g_nightColour = math::Color(9.0f / 255.0f, 14.0f / 255.0f, 44.0f / 255.0f);

	void DayNightCycle::Update(float frameTime, HexEngine::DirectionalLight* sunLight)
	{
		if (!sunLight)
			return;

		if (env_simulateTime._val.b)
		{
			env_time._val.f32 += frameTime * DayCycleSpeed;
		}

		if (env_time._val.f32 >= 1.0f)
			env_time._val.f32 -= 1.0f;

		const float nightStart = 0.24f;
		const float nightEnd = 0.3f;
		const float nightDelta = nightEnd - nightStart;

		if(env_time._val.f32 >= nightStart && env_time._val.f32 <= nightEnd)
		{
			bool a = false;
			float lightRangeVal = 1.0f - ((env_time._val.f32 - nightStart) / nightDelta);

			_lightMultiplier = lightRangeVal;

			sunLight->SetLightMultiplier(_lightMultiplier);
		}

		if (env_time._val.f32 >= 0.75f && env_time._val.f32 <= 0.80f)
		{
			bool a = false;
			float lightRangeVal = ((env_time._val.f32 - 0.75f) / 0.05f);

			_lightMultiplier = lightRangeVal;

			sunLight->SetLightMultiplier(_lightMultiplier);
		}

		math::Color::Lerp(g_nightColour, g_dayColour, _lightMultiplier, _ambientColour);

		HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->SetAmbientLight(_ambientColour);

		//HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->SetFogColour(_ambientColour);

		float pitch = -90.0f + (360.0f * env_time._val.f32);

		if (pitch > 360.0f)
			pitch -= 360.0f;

		auto sunTransform = sunLight->GetComponent<HexEngine::Transform>();

		auto rotation = math::Matrix::CreateFromYawPitchRoll(ToRadian(55), ToRadian(pitch), ToRadian(7.0f));

		sunTransform->SetRotation(math::Quaternion::CreateFromRotationMatrix(rotation));

		auto lookDir = math::Vector3::Transform(math::Vector3::Forward, rotation);
		lookDir.Normalize();

		auto lightPosCenter = math::Vector3(0, 0, 0);

		auto newPosition = lightPosCenter - (lookDir * env_sunDistance._val.f32);

		sunTransform->SetPosition(newPosition);
		sunTransform->SetScale(math::Vector3(20.0f));
	}
}