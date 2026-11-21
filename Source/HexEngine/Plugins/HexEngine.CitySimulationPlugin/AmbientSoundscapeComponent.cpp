#include "AmbientSoundscapeComponent.hpp"
#include "AmbientZoneComponent.hpp"
#include "AmbientEmitterComponent.hpp"
#include "TrafficVehicleComponent.hpp"
#include <HexEngine.Core/Audio/AudioManager.hpp>
#include <HexEngine.Core/Entity/Entity.hpp>
#include <HexEngine.Core/Entity/Component/Camera.hpp>
#include <HexEngine.Core/Entity/Component/DayNightCycleComponent.hpp>
#include <HexEngine.Core/GUI/Elements/AssetSearch.hpp>
#include <HexEngine.Core/GUI/Elements/Checkbox.hpp>
#include <HexEngine.Core/GUI/Elements/ComponentWidget.hpp>
#include <HexEngine.Core/GUI/Elements/DragFloat.hpp>
#include <HexEngine.Core/GUI/Elements/DragInt.hpp>
#include <HexEngine.Core/GUI/Elements/EntitySearch.hpp>
#include <HexEngine.Core/Physics/PhysUtils.hpp>
#include <HexEngine.Core/Scene/Scene.hpp>
#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace
{
	constexpr float kSilent = 0.0005f;

	float Approach(float value, float target, float maxStep)
	{
		return value + std::clamp(target - value, -maxStep, maxStep);
	}
}

AmbientSoundscapeComponent::AmbientSoundscapeComponent(HexEngine::Entity* entity) :
	UpdateComponent(entity)
{
}

AmbientSoundscapeComponent::AmbientSoundscapeComponent(HexEngine::Entity* entity, AmbientSoundscapeComponent* copy) :
	UpdateComponent(entity)
{
	if (copy != nullptr)
	{
		_previewInEditor = copy->_previewInEditor;
		_masterVolume = copy->_masterVolume;
		_fadeSeconds = copy->_fadeSeconds;
		_dayStartHour = copy->_dayStartHour;
		_nightStartHour = copy->_nightStartHour;
		_transitionHours = copy->_transitionHours;
		_indoorExteriorScale = copy->_indoorExteriorScale;
		_maxEmitterVoices = copy->_maxEmitterVoices;
		_trafficDayBed = copy->_trafficDayBed;
		_trafficNightBed = copy->_trafficNightBed;
		_trafficVolume = copy->_trafficVolume;
		_trafficRadius = copy->_trafficRadius;
		_trafficFullDensity = copy->_trafficFullDensity;
		_hourBell = copy->_hourBell;
		_bellEntityName = copy->_bellEntityName;
		_bellVolume = copy->_bellVolume;
		_bellRadius = copy->_bellRadius;
		_bellFirstHour = copy->_bellFirstHour;
		_bellLastHour = copy->_bellLastHour;
	}
}

AmbientSoundscapeComponent::~AmbientSoundscapeComponent()
{
	StopAll();
}

void AmbientSoundscapeComponent::Destroy()
{
	StopAll();
	UpdateComponent::Destroy();
}

// ---------------------------------------------------------------------------

void AmbientSoundscapeComponent::StopAll()
{
	auto* audio = HexEngine::g_pEnv != nullptr ? HexEngine::g_pEnv->_audioManager : nullptr;
	auto stop = [audio](const std::shared_ptr<HexEngine::SoundEffect>& s)
	{
		if (audio != nullptr && s != nullptr)
			audio->Stop(s);
	};
	for (auto& [path, v] : _beds) stop(v.sound);
	for (auto& [key, v] : _emitterVoices) stop(v.sound);
	for (auto& os : _oneShots) stop(os.sound);
	_beds.clear();
	_emitterVoices.clear();
	_oneShots.clear();
	_oneShotTimers.clear();
	_trafficDensity = 0.0f;
	_lastBellHour = -1;
}

std::shared_ptr<HexEngine::SoundEffect> AmbientSoundscapeComponent::MakeVoice(const std::string& path)
{
	if (path.empty())
		return nullptr;
	auto it = _masters.find(path);
	if (it == _masters.end())
	{
		auto master = HexEngine::SoundEffect::Create(path);
		if (master == nullptr)
			LOG_WARN("AmbientSoundscape: could not load '%s' (it will be skipped)", path.c_str());
		it = _masters.emplace(path, master).first;   // a failed load is remembered as null
	}
	return it->second != nullptr ? it->second->CreatePlaybackClone() : nullptr;
}

float AmbientSoundscapeComponent::ComputeNightFactor(float hour) const
{
	// 0 by day, 1 by night; _transitionHours-long ramps starting at dawn/dusk.
	const float blend = std::max(0.01f, _transitionHours);
	auto inWindow = [](float h, float start, float len)
	{
		float d = std::fmod(h - start + 48.0f, 24.0f);
		return d >= 0.0f && d < len ? d / len : -1.0f;
	};
	if (const float dawn = inWindow(hour, _dayStartHour, blend); dawn >= 0.0f)
		return 1.0f - dawn;
	if (const float dusk = inWindow(hour, _nightStartHour, blend); dusk >= 0.0f)
		return dusk;
	const float dayLen = std::fmod(_nightStartHour - (_dayStartHour + blend) + 48.0f, 24.0f);
	return inWindow(hour, _dayStartHour + blend, dayLen) >= 0.0f ? 0.0f : 1.0f;
}

float AmbientSoundscapeComponent::TimeMaskWeight(int32_t mask) const
{
	switch (mask)
	{
	case 1: return 1.0f - _night;
	case 2: return _night;
	default: return 1.0f;
	}
}

// ---------------------------------------------------------------------------

void AmbientSoundscapeComponent::Update(float frameTime)
{
	UpdateComponent::Update(frameTime);

	auto* entity = GetEntity();
	auto* scene = entity != nullptr ? entity->GetScene() : nullptr;
	auto* audio = HexEngine::g_pEnv != nullptr ? HexEngine::g_pEnv->_audioManager : nullptr;
	const bool running = scene != nullptr && audio != nullptr &&
		(HexEngine::g_pEnv->IsGameRunning() || (_previewInEditor && HexEngine::g_pEnv->IsEditorMode()));
	if (!running)
	{
		if (_wasRunning)
			StopAll();
		_wasRunning = false;
		return;
	}
	if (!_wasRunning)
	{
		_lastBellHour = -1;      // no bell just for starting play mid-hour
		_wasRunning = true;
	}

	auto* camera = scene->GetMainCamera();
	if (camera == nullptr || camera->GetEntity() == nullptr)
		return;
	HexEngine::Entity* listenerEntity = camera->GetEntity();
	const math::Vector3 listener = listenerEntity->GetWorldTM().Translation();
	const float dt = std::clamp(frameTime, 0.0f, 0.25f);

	// --- world state ---
	float hour = 12.0f;
	{
		std::vector<HexEngine::DayNightCycleComponent*> clocks;
		scene->GetComponents<HexEngine::DayNightCycleComponent>(clocks);
		if (!clocks.empty() && clocks[0] != nullptr)
			hour = clocks[0]->GetCurrentTimeHours();
	}
	_night = ComputeNightFactor(hour);
	const auto& weather = scene->GetWeatherSurfaceParams();
	_rain = Approach(_rain, std::clamp(weather.precipitationIntensity, 0.0f, 1.0f), dt * 0.5f);
	_wind = Approach(_wind, std::clamp(weather.windDirectionAndSpeed.w / 30.0f, 0.0f, 1.0f), dt * 0.5f);
	UpdateIndoor(scene, listenerEntity, listener, dt);
	const float exteriorScale = 1.0f + (_indoorExteriorScale - 1.0f) * _indoor;

	// --- zones -> bed targets ---
	std::vector<AmbientZoneComponent*> zones;
	scene->GetComponents<AmbientZoneComponent>(zones);
	std::stable_sort(zones.begin(), zones.end(), [](AmbientZoneComponent* a, AmbientZoneComponent* b)
		{ return a->GetPriority() > b->GetPriority(); });

	std::unordered_map<std::string, float> targets;
	std::vector<std::pair<AmbientZoneComponent*, float>> zoneWeights;
	float remaining = 1.0f;
	for (auto* zone : zones)
	{
		if (zone == nullptr || zone->GetEntity() == nullptr || zone->GetEntity()->IsPendingDeletion())
			continue;
		float w = zone->ComputeWeight(listener);
		if (zone->IsInterior())
			w *= _indoor;
		const float eff = w * remaining;
		remaining *= 1.0f - std::clamp(w * zone->GetOcclusion(), 0.0f, 1.0f);
		if (eff <= kSilent)
			continue;
		zoneWeights.emplace_back(zone, eff);

		const float exterior = zone->IsInterior() ? 1.0f : exteriorScale;
		const float gain = eff * zone->GetBedVolume() * (1.0f - zone->GetRainDuck() * _rain) * exterior;
		const std::string& day = zone->GetDayBed();
		const std::string& night = zone->GetNightBed();
		// A day-only bed fades out at night and vice versa; a zone that should
		// sound the same around the clock sets the same bed for both.
		if (!day.empty())
			targets[day] += gain * (1.0f - _night);
		if (!night.empty())
			targets[night] += gain * _night;
	}

	// --- traffic hum ---
	UpdateTraffic(listener, dt);
	if (_trafficDensity > kSilent)
	{
		const float gain = _trafficDensity * _trafficVolume * exteriorScale;
		if (!_trafficDayBed.empty() && !_trafficNightBed.empty())
		{
			targets[_trafficDayBed] += gain * (1.0f - _night);
			targets[_trafficNightBed] += gain * _night;
		}
		else if (!_trafficDayBed.empty())
			targets[_trafficDayBed] += gain;
	}

	UpdateBeds(targets, dt);
	UpdateEmitters(scene, listener, dt);
	UpdateOneShots(zoneWeights, listener, dt);
	UpdateHourBell(scene, hour);
}

void AmbientSoundscapeComponent::UpdateIndoor(HexEngine::Scene* scene, HexEngine::Entity* listenerEntity, const math::Vector3& listener, float dt)
{
	// A roof COLLIDER overhead = indoors. Physics only: tree props carry trunk
	// colliders, so a canopy doesn't count, while building shells do.
	_roofProbeCooldown -= dt;
	if (_roofProbeCooldown <= 0.0f)
	{
		_roofProbeCooldown = 0.25f;
		HexEngine::RayCastOptions options;
		options.maxDistance = 40.0f;
		options.meshes = false;
		options.physics = true;
		options.hierarchical = false;
		for (auto* e = listenerEntity; e != nullptr; e = e->GetParent())
			options.ignore.insert(e);
		if (auto* player = scene->GetEntityByName("Player"); player != nullptr)
			options.ignore.insert(player);
		HexEngine::RayHit hit;
		_roofOverhead = HexEngine::PhysUtils::RayCast(scene, listener + math::Vector3(0.0f, 0.1f, 0.0f), math::Vector3::Up, options, &hit);
	}
	_indoor = Approach(_indoor, _roofOverhead ? 1.0f : 0.0f, dt * 2.0f);
}

void AmbientSoundscapeComponent::UpdateTraffic(const math::Vector3& listener, float dt)
{
	_trafficCooldown -= dt;
	if (_trafficCooldown <= 0.0f)
	{
		_trafficCooldown = 0.3f;
		std::vector<TrafficVehicleComponent*> nearby;
		TrafficVehicleComponent::QueryNearbyVehicles(listener, _trafficRadius, nearby);
		float sum = 0.0f;
		for (auto* v : nearby)
		{
			if (v == nullptr || v->GetEntity() == nullptr || v->IsParked() || v->GetCurrentSpeed() < 0.5f)
				continue;
			const float d = math::Vector3::Distance(v->GetEntity()->GetWorldTM().Translation(), listener);
			sum += std::max(0.0f, 1.0f - d / std::max(1.0f, _trafficRadius));
		}
		_trafficTarget = std::clamp(sum / std::max(0.1f, _trafficFullDensity), 0.0f, 1.0f);
	}
	// Slow: the hum should swell and settle, not pump with each passing car.
	_trafficDensity = Approach(_trafficDensity, _trafficTarget, dt * 0.25f);
}

void AmbientSoundscapeComponent::UpdateBeds(const std::unordered_map<std::string, float>& targets, float dt)
{
	auto* audio = HexEngine::g_pEnv->_audioManager;
	for (const auto& [path, gain] : targets)
	{
		if (gain > kSilent && _beds.find(path) == _beds.end())
			_beds.emplace(path, Voice{});
	}
	const float step = dt / std::max(0.05f, _fadeSeconds);
	for (auto it = _beds.begin(); it != _beds.end(); )
	{
		auto found = targets.find(it->first);
		const float target = std::clamp((found != targets.end() ? found->second : 0.0f) * _masterVolume, 0.0f, 2.0f);
		Voice& v = it->second;
		v.gain = Approach(v.gain, target, step);
		if (!v.playing && v.gain > kSilent)
		{
			v.sound = MakeVoice(it->first);
			if (v.sound == nullptr)
			{
				it = _beds.erase(it);
				continue;
			}
			audio->Loop(v.sound);       // 2D: stereo bed, never spatialised
			v.playing = true;
		}
		if (v.playing)
			v.sound->SetVolume(v.gain);
		if (v.gain <= kSilent && target <= kSilent)
		{
			if (v.playing)
				audio->Stop(v.sound);
			it = _beds.erase(it);
			continue;
		}
		++it;
	}
}

void AmbientSoundscapeComponent::UpdateEmitters(HexEngine::Scene* scene, const math::Vector3& listener, float dt)
{
	auto* audio = HexEngine::g_pEnv->_audioManager;
	std::vector<AmbientEmitterComponent*> all;
	scene->GetComponents<AmbientEmitterComponent>(all);

	struct Candidate { AmbientEmitterComponent* emitter; math::Vector3 pos; float dist; float timeWeight; };
	std::vector<Candidate> candidates;
	for (auto* e : all)
	{
		if (e == nullptr || e->GetEntity() == nullptr || e->GetEntity()->IsPendingDeletion() || e->GetSoundPath().empty())
			continue;
		const float tw = TimeMaskWeight(e->GetTimes());
		if (tw <= 0.01f)
			continue;
		const math::Vector3 pos = math::Vector3::Transform(e->GetOffset(), e->GetEntity()->GetWorldTM());
		const float d = math::Vector3::Distance(pos, listener);
		if (d < e->GetRadius())
			candidates.push_back({ e, pos, d, tw });
	}
	const size_t budget = (size_t)std::max(0, _maxEmitterVoices);
	if (candidates.size() > budget)
	{
		std::partial_sort(candidates.begin(), candidates.begin() + budget, candidates.end(),
			[](const Candidate& a, const Candidate& b) { return a.dist < b.dist; });
		candidates.resize(budget);
	}

	const float exteriorScale = 1.0f + (_indoorExteriorScale - 1.0f) * _indoor;
	const float step = dt / 1.0f;
	std::unordered_set<AmbientEmitterComponent*> chosen;
	for (const auto& c : candidates)
	{
		chosen.insert(c.emitter);
		Voice& v = _emitterVoices[c.emitter];
		const float windTerm = 1.0f + ((0.25f + 0.75f * _wind) - 1.0f) * c.emitter->GetWindScale();
		const float rainTerm = 1.0f + (c.emitter->GetRainScale() - 1.0f) * _rain;
		const float target = std::clamp(c.emitter->GetVolume() * c.timeWeight * windTerm * rainTerm * exteriorScale * _masterVolume, 0.0f, 2.0f);
		if (!v.playing)
		{
			v.sound = MakeVoice(c.emitter->GetSoundPath());
			if (v.sound == nullptr)
				continue;
			v.sound->SetRadius(c.emitter->GetRadius());
			v.sound->SetVolume(0.0f);
			audio->Loop(v.sound, c.pos);
			v.playing = true;
			v.gain = 0.0f;
		}
		v.gain = Approach(v.gain, target, step);
		v.sound->SetVolume(v.gain);
		v.sound->SetPosition(c.pos);
	}
	// Voices that lost their slot (out of range / budget) fade out. The key is
	// never dereferenced, so a since-deleted emitter is harmless here.
	for (auto it = _emitterVoices.begin(); it != _emitterVoices.end(); )
	{
		if (chosen.count(it->first) == 0)
		{
			Voice& v = it->second;
			v.gain = Approach(v.gain, 0.0f, step * 2.0f);
			if (v.sound != nullptr)
				v.sound->SetVolume(v.gain);
			if (v.gain <= kSilent || v.sound == nullptr)
			{
				if (v.sound != nullptr)
					audio->Stop(v.sound);
				it = _emitterVoices.erase(it);
				continue;
			}
		}
		++it;
	}
}

void AmbientSoundscapeComponent::UpdateOneShots(const std::vector<std::pair<AmbientZoneComponent*, float>>& zoneWeights, const math::Vector3& listener, float dt)
{
	for (auto it = _oneShots.begin(); it != _oneShots.end(); )
	{
		it->remaining -= dt;
		if (it->remaining <= 0.0f)
			it = _oneShots.erase(it);
		else
			++it;
	}

	const float exteriorScale = 1.0f + (_indoorExteriorScale - 1.0f) * _indoor;
	std::unordered_set<AmbientZoneComponent*> live;
	for (const auto& [zone, eff] : zoneWeights)
	{
		live.insert(zone);
		const auto& shots = zone->GetOneShots();
		if (shots.empty() || eff < 0.3f || TimeMaskWeight(static_cast<int32_t>(zone->GetOneShotTimes())) < 0.5f)
			continue;
		const float lo = std::max(0.5f, zone->GetOneShotMinInterval());
		const float hi = std::max(lo, zone->GetOneShotMaxInterval());
		auto timer = _oneShotTimers.find(zone);
		if (timer == _oneShotTimers.end())
			timer = _oneShotTimers.emplace(zone, std::uniform_real_distribution<float>(lo * 0.3f, hi)(_rng)).first;
		timer->second -= dt;
		if (timer->second > 0.0f)
			continue;
		timer->second = std::uniform_real_distribution<float>(lo, hi)(_rng);

		const std::string& path = shots[std::uniform_int_distribution<size_t>(0, shots.size() - 1)(_rng)];
		const float angle = std::uniform_real_distribution<float>(0.0f, 6.28318530718f)(_rng);
		const float dmin = std::max(0.0f, zone->GetOneShotMinDistance());
		const float dist = std::uniform_real_distribution<float>(dmin, std::max(dmin, zone->GetOneShotMaxDistance()))(_rng);
		const math::Vector3 pos = listener + math::Vector3(std::cos(angle) * dist, 0.0f, std::sin(angle) * dist);
		const float exterior = zone->IsInterior() ? 1.0f : exteriorScale;
		PlayOneShot(path, pos, zone->GetOneShotVolume() * std::min(1.0f, eff) * exterior * _masterVolume, zone->GetOneShotRadius());
	}
	for (auto it = _oneShotTimers.begin(); it != _oneShotTimers.end(); )
	{
		if (live.count(it->first) == 0)
			it = _oneShotTimers.erase(it);
		else
			++it;
	}
}

void AmbientSoundscapeComponent::UpdateHourBell(HexEngine::Scene* scene, float hour)
{
	if (_hourBell.empty())
		return;
	const int32_t h = ((int32_t)std::floor(hour) % 24 + 24) % 24;
	if (_lastBellHour < 0)
	{
		_lastBellHour = h;
		return;
	}
	if (h == _lastBellHour)
		return;
	_lastBellHour = h;
	if (h < _bellFirstHour || h > _bellLastHour)
		return;

	math::Vector3 pos;
	if (auto* bell = !_bellEntityName.empty() ? scene->GetEntityByName(_bellEntityName) : nullptr; bell != nullptr)
		pos = bell->GetWorldTM().Translation() + math::Vector3(0.0f, 20.0f, 0.0f);
	else if (auto* camera = scene->GetMainCamera(); camera != nullptr && camera->GetEntity() != nullptr)
		pos = camera->GetEntity()->GetWorldTM().Translation() + math::Vector3(0.0f, 30.0f, -150.0f);
	const float exteriorScale = 1.0f + (_indoorExteriorScale - 1.0f) * _indoor;
	PlayOneShot(_hourBell, pos, _bellVolume * exteriorScale * _masterVolume, _bellRadius);
}

void AmbientSoundscapeComponent::PlayOneShot(const std::string& path, const math::Vector3& position, float volume, float radius)
{
	if (volume <= kSilent)
		return;
	auto sound = MakeVoice(path);
	if (sound == nullptr)
		return;
	sound->SetRadius(radius);
	sound->SetVolume(std::clamp(volume, 0.0f, 2.0f));
	HexEngine::g_pEnv->_audioManager->Play(sound, position);
	// Keep the clone alive until it has finished (the AudioManager only holds
	// a weak reference for its per-frame 3D update).
	_oneShots.push_back({ sound, std::max(1.0f, sound->GetDuration() + 0.5f) });
}

// ---------------------------------------------------------------------------

void AmbientSoundscapeComponent::Serialize(json& data, HexEngine::JsonFile* file)
{
	file->Serialize(data, "_previewInEditor", _previewInEditor);
	file->Serialize(data, "_masterVolume", _masterVolume);
	file->Serialize(data, "_fadeSeconds", _fadeSeconds);
	file->Serialize(data, "_dayStartHour", _dayStartHour);
	file->Serialize(data, "_nightStartHour", _nightStartHour);
	file->Serialize(data, "_transitionHours", _transitionHours);
	file->Serialize(data, "_indoorExteriorScale", _indoorExteriorScale);
	file->Serialize(data, "_maxEmitterVoices", _maxEmitterVoices);
	file->Serialize(data, "_trafficDayBed", _trafficDayBed);
	file->Serialize(data, "_trafficNightBed", _trafficNightBed);
	file->Serialize(data, "_trafficVolume", _trafficVolume);
	file->Serialize(data, "_trafficRadius", _trafficRadius);
	file->Serialize(data, "_trafficFullDensity", _trafficFullDensity);
	file->Serialize(data, "_hourBell", _hourBell);
	file->Serialize(data, "_bellEntityName", _bellEntityName);
	file->Serialize(data, "_bellVolume", _bellVolume);
	file->Serialize(data, "_bellRadius", _bellRadius);
	file->Serialize(data, "_bellFirstHour", _bellFirstHour);
	file->Serialize(data, "_bellLastHour", _bellLastHour);
}

void AmbientSoundscapeComponent::Deserialize(json& data, HexEngine::JsonFile* file, uint32_t mask)
{
	(void)mask;
	file->Deserialize(data, "_previewInEditor", _previewInEditor);
	file->Deserialize(data, "_masterVolume", _masterVolume);
	file->Deserialize(data, "_fadeSeconds", _fadeSeconds);
	file->Deserialize(data, "_dayStartHour", _dayStartHour);
	file->Deserialize(data, "_nightStartHour", _nightStartHour);
	file->Deserialize(data, "_transitionHours", _transitionHours);
	file->Deserialize(data, "_indoorExteriorScale", _indoorExteriorScale);
	file->Deserialize(data, "_maxEmitterVoices", _maxEmitterVoices);
	file->Deserialize(data, "_trafficDayBed", _trafficDayBed);
	file->Deserialize(data, "_trafficNightBed", _trafficNightBed);
	file->Deserialize(data, "_trafficVolume", _trafficVolume);
	file->Deserialize(data, "_trafficRadius", _trafficRadius);
	file->Deserialize(data, "_trafficFullDensity", _trafficFullDensity);
	file->Deserialize(data, "_hourBell", _hourBell);
	file->Deserialize(data, "_bellEntityName", _bellEntityName);
	file->Deserialize(data, "_bellVolume", _bellVolume);
	file->Deserialize(data, "_bellRadius", _bellRadius);
	file->Deserialize(data, "_bellFirstHour", _bellFirstHour);
	file->Deserialize(data, "_bellLastHour", _bellLastHour);
}

bool AmbientSoundscapeComponent::CreateWidget(HexEngine::ComponentWidget* widget)
{
	using namespace HexEngine;
	const int32_t w = widget->GetSize().x - 20;
	new Checkbox(widget, widget->GetNextPos(), Point(w, 18), L"Preview in editor", &_previewInEditor);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Master Volume", &_masterVolume, 0.0f, 2.0f, 0.01f, 2);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Bed Fade (s)", &_fadeSeconds, 0.05f, 20.0f, 0.05f, 2);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Dawn (h)", &_dayStartHour, 0.0f, 24.0f, 0.05f, 2);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Dusk (h)", &_nightStartHour, 0.0f, 24.0f, 0.05f, 2);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Dawn/Dusk Ramp (h)", &_transitionHours, 0.05f, 6.0f, 0.05f, 2);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Indoor Exterior Scale", &_indoorExteriorScale, 0.0f, 1.0f, 0.01f, 2);
	new DragInt(widget, widget->GetNextPos(), Point(w, 18), L"Max Emitter Voices", &_maxEmitterVoices, 0, 64, 1);

	auto bindSound = [&](const wchar_t* label, std::string& target)
	{
		auto* search = new AssetSearch(widget, widget->GetNextPos(), Point(w, 84), label, { ResourceType::Audio },
			[&target](AssetSearch*, const AssetSearchResult& result)
			{
				const fs::path& chosen = !result.assetPath.empty() ? result.assetPath : result.absolutePath;
				target = chosen.string();
			});
		if (!target.empty())
			search->SetValue(std::wstring(target.begin(), target.end()));
	};
	bindSound(L"Traffic Hum (day)", _trafficDayBed);
	bindSound(L"Traffic Hum (night)", _trafficNightBed);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Traffic Volume", &_trafficVolume, 0.0f, 2.0f, 0.01f, 2);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Traffic Radius (m)", &_trafficRadius, 5.0f, 500.0f, 1.0f, 0);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Traffic Full Density", &_trafficFullDensity, 0.5f, 50.0f, 0.1f, 1);

	bindSound(L"Hour Bell", _hourBell);
	auto* bellEntity = new EntitySearch(widget, widget->GetNextPos(), Point(w, 18), L"Bell Entity");
	bellEntity->SetValue(std::wstring(_bellEntityName.begin(), _bellEntityName.end()));
	bellEntity->SetOnInputFn([this](EntitySearch*, const std::wstring& value) { _bellEntityName = ws2s(value); });
	bellEntity->SetOnSelectFn([this](EntitySearch*, const EntitySearchResult& result) { _bellEntityName = result.entityName; });
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Bell Volume", &_bellVolume, 0.0f, 2.0f, 0.01f, 2);
	new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Bell Radius (m)", &_bellRadius, 10.0f, 3000.0f, 5.0f, 0);
	new DragInt(widget, widget->GetNextPos(), Point(w, 18), L"Bell First Hour", &_bellFirstHour, 0, 23, 1);
	new DragInt(widget, widget->GetNextPos(), Point(w, 18), L"Bell Last Hour", &_bellLastHour, 0, 23, 1);
	return true;
}
