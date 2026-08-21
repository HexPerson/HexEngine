#include "SoundEffectComponent.hpp"
#include "../Entity.hpp"
#include "Transform.hpp"
#include "../../Environment/IEnvironment.hpp"
#include "../../Environment/LogFile.hpp"
#include "../../Graphics/DebugRenderer.hpp"
#include "../../Audio/AudioManager.hpp"
#include "../../GUI/Elements/ComponentWidget.hpp"
#include "../../GUI/Elements/DragFloat.hpp"
#include "../../GUI/Elements/Checkbox.hpp"
#include "../../GUI/Elements/Button.hpp"
#include "../../GUI/Elements/AssetSearch.hpp"
#include <algorithm>
#include <cmath>

namespace HexEngine
{
	SoundEffectComponent::SoundEffectComponent(Entity* entity) :
		UpdateComponent(entity)
	{
	}

	SoundEffectComponent::SoundEffectComponent(Entity* entity, SoundEffectComponent* copy) :
		UpdateComponent(entity)
	{
		if (copy != nullptr)
		{
			_soundPath   = copy->_soundPath;
			_is3D        = copy->_is3D;
			_loop        = copy->_loop;
			_playOnStart = copy->_playOnStart;
			_volume      = copy->_volume;
			_pitch       = copy->_pitch;
			_radius      = copy->_radius;
		}
	}

	SoundEffectComponent::~SoundEffectComponent()
	{
		ReleaseInstance();
	}

	void SoundEffectComponent::Destroy()
	{
		ReleaseInstance();
	}

	// ---- instance lifecycle ---------------------------------------------------

	bool SoundEffectComponent::EnsureInstance()
	{
		if (_instance != nullptr)
			return true;
		if (_soundPath.empty())
			return false;

		if (_master == nullptr)
		{
			_master = SoundEffect::Create(_soundPath);
			if (_master == nullptr)
			{
				LOG_WARN("SoundEffectComponent: could not load sound '%s'", _soundPath.c_str());
				return false;
			}
		}

		// A private playback clone: the master is a shared, cached resource and
		// its single instance would be fought over by every entity using the
		// same .wav. The clone self-registers for the AudioManager's per-frame
		// Apply3D, which is what lets a moving emitter track its entity.
		_instance = _master->CreatePlaybackClone();
		if (_instance == nullptr)
		{
			LOG_WARN("SoundEffectComponent: could not create a playback instance for '%s'", _soundPath.c_str());
			return false;
		}

		ApplyParameters();
		return true;
	}

	void SoundEffectComponent::ReleaseInstance()
	{
		if (_instance != nullptr)
		{
			if (g_pEnv != nullptr && g_pEnv->_audioManager != nullptr)
				g_pEnv->_audioManager->Stop(_instance);
			_instance.reset();
		}
		_master.reset();
		_startedThisRun = false;
	}

	void SoundEffectComponent::ApplyParameters()
	{
		if (_instance == nullptr)
			return;
		_instance->SetVolume(std::clamp(_volume, 0.0f, 1.0f));
		_instance->SetPitch(std::clamp(_pitch, -1.0f, 1.0f));
		_instance->SetRadius(std::max(_radius, 0.01f));
	}

	math::Vector3 SoundEffectComponent::GetEmitterPosition() const
	{
		auto* ent = GetEntity();
		return ent != nullptr ? ent->GetWorldTM().Translation() : math::Vector3::Zero;
	}

	// ---- playback ---------------------------------------------------------------

	void SoundEffectComponent::Play()
	{
		if (!EnsureInstance())
			return;
		if (g_pEnv == nullptr || g_pEnv->_audioManager == nullptr)
			return;

		ApplyParameters();

		// The AudioManager overloads decide 2D vs 3D: the positional variants
		// flag the emitter and Apply3D it, the plain ones clear the flag.
		if (_is3D)
		{
			const math::Vector3 pos = GetEmitterPosition();
			if (_loop)
				g_pEnv->_audioManager->Loop(_instance, pos);
			else
				g_pEnv->_audioManager->Play(_instance, pos);
		}
		else
		{
			if (_loop)
				g_pEnv->_audioManager->Loop(_instance);
			else
				g_pEnv->_audioManager->Play(_instance);
		}
	}

	void SoundEffectComponent::Stop()
	{
		if (_instance != nullptr && g_pEnv != nullptr && g_pEnv->_audioManager != nullptr)
			g_pEnv->_audioManager->Stop(_instance);
	}

	bool SoundEffectComponent::IsPlaying() const
	{
		return _instance != nullptr && _instance->IsPlaying();
	}

	void SoundEffectComponent::Update(float frameTime)
	{
		UpdateComponent::Update(frameTime);

		const bool gameRunning = g_pEnv != nullptr && g_pEnv->IsGameRunning();

		// Play-on-start fires once per play session; leaving play mode stops
		// the sound and re-arms it so the next session starts clean instead of
		// inheriting a looping emitter from the last one.
		if (gameRunning && !_wasGameRunning)
			_startedThisRun = false;
		if (!gameRunning && _wasGameRunning)
		{
			Stop();
			_startedThisRun = false;
		}
		_wasGameRunning = gameRunning;

		if (gameRunning && _playOnStart && !_startedThisRun)
		{
			_startedThisRun = true;
			Play();
		}

		// Keep a 3D emitter glued to its entity. SetPosition only updates the
		// emitter; the AudioManager re-applies 3D for registered instances every
		// frame, so this moves the sound without restarting it.
		if (_is3D && _instance != nullptr && _instance->IsPlaying())
			_instance->SetPosition(GetEmitterPosition());
	}

	// ---- setters (live) ---------------------------------------------------------

	void SoundEffectComponent::SetSoundPath(const std::string& path)
	{
		if (path == _soundPath)
			return;
		// A different asset invalidates the cached master and its clone; the
		// next Play() reloads. If it was audible, restart with the new sound.
		const bool wasPlaying = IsPlaying();
		ReleaseInstance();
		_soundPath = path;
		if (wasPlaying)
			Play();
	}

	void SoundEffectComponent::SetIs3D(bool is3D)
	{
		if (is3D == _is3D)
			return;
		_is3D = is3D;
		// 2D/3D is chosen at Play() time by the AudioManager overload used, so a
		// currently audible sound has to be restarted to switch modes.
		if (IsPlaying())
			Play();
	}

	void SoundEffectComponent::SetLoop(bool loop)
	{
		if (loop == _loop)
			return;
		_loop = loop;
		if (IsPlaying())
			Play();
	}

	void SoundEffectComponent::SetVolume(float volume)
	{
		_volume = std::clamp(volume, 0.0f, 1.0f);
		if (_instance != nullptr)
			_instance->SetVolume(_volume);
	}

	void SoundEffectComponent::SetPitch(float pitch)
	{
		_pitch = std::clamp(pitch, -1.0f, 1.0f);
		if (_instance != nullptr)
			_instance->SetPitch(_pitch);
	}

	void SoundEffectComponent::SetRadius(float radius)
	{
		_radius = std::max(radius, 0.01f);
		// The emitter's distance scaler is read by the per-frame Apply3D, so
		// this takes effect on a playing sound without a restart.
		if (_instance != nullptr)
			_instance->SetRadius(_radius);
	}

	// ---- serialization ------------------------------------------------------------

	void SoundEffectComponent::Serialize(json& data, JsonFile* file)
	{
		SERIALIZE_VALUE(_soundPath);
		SERIALIZE_VALUE(_is3D);
		SERIALIZE_VALUE(_loop);
		SERIALIZE_VALUE(_playOnStart);
		SERIALIZE_VALUE(_volume);
		SERIALIZE_VALUE(_pitch);
		SERIALIZE_VALUE(_radius);
	}

	void SoundEffectComponent::Deserialize(json& data, JsonFile* file, uint32_t mask)
	{
		(void)mask;
		DESERIALIZE_VALUE(_soundPath);
		DESERIALIZE_VALUE(_is3D);
		DESERIALIZE_VALUE(_loop);
		DESERIALIZE_VALUE(_playOnStart);
		DESERIALIZE_VALUE(_volume);
		DESERIALIZE_VALUE(_pitch);
		DESERIALIZE_VALUE(_radius);

		_volume = std::clamp(_volume, 0.0f, 1.0f);
		_pitch  = std::clamp(_pitch, -1.0f, 1.0f);
		_radius = std::max(_radius, 0.01f);
	}

	// ---- editor -----------------------------------------------------------------------

	bool SoundEffectComponent::CreateWidget(ComponentWidget* widget)
	{
		const int32_t fullWidth = widget->GetSize().x - 20;

		auto* soundSearch = new AssetSearch(widget, widget->GetNextPos(), Point(fullWidth, 84),
			L"Sound", { ResourceType::Audio },
			[this](AssetSearch*, const AssetSearchResult& result)
			{
				const fs::path& chosen = !result.assetPath.empty() ? result.assetPath : result.absolutePath;
				SetSoundPath(chosen.string());
			});
		if (!_soundPath.empty())
			soundSearch->SetValue(std::wstring(_soundPath.begin(), _soundPath.end()));

		// Bound-bool checkboxes (the callback flavour is a getter, polled every
		// render). The on-check hook restarts an audible sound so mode changes
		// apply live; the bool itself is already flipped by the time it fires.
		auto* is3D = new Checkbox(widget, widget->GetNextPos(), Point(fullWidth, 18), L"3D (positional)", &_is3D);
		is3D->SetPrefabOverrideBinding(GetComponentName(), "/_is3D");
		is3D->SetOnCheckFn([this](Checkbox*, bool) { if (IsPlaying()) Play(); });

		auto* loop = new Checkbox(widget, widget->GetNextPos(), Point(fullWidth, 18), L"Loop", &_loop);
		loop->SetPrefabOverrideBinding(GetComponentName(), "/_loop");
		loop->SetOnCheckFn([this](Checkbox*, bool) { if (IsPlaying()) Play(); });

		auto* playOnStart = new Checkbox(widget, widget->GetNextPos(), Point(fullWidth, 18), L"Play On Start", &_playOnStart);
		playOnStart->SetPrefabOverrideBinding(GetComponentName(), "/_playOnStart");

		auto* volume = new DragFloat(widget, widget->GetNextPos(), Point(fullWidth, 18), L"Volume",
			&_volume, 0.0f, 1.0f, 0.005f, 2);
		volume->SetPrefabOverrideBinding(GetComponentName(), "/_volume");
		volume->SetOnDrag([this](float value, float, float) { SetVolume(value); });

		auto* pitch = new DragFloat(widget, widget->GetNextPos(), Point(fullWidth, 18), L"Pitch (octaves)",
			&_pitch, -1.0f, 1.0f, 0.005f, 2);
		pitch->SetPrefabOverrideBinding(GetComponentName(), "/_pitch");
		pitch->SetOnDrag([this](float value, float, float) { SetPitch(value); });

		auto* radius = new DragFloat(widget, widget->GetNextPos(), Point(fullWidth, 18), L"Radius (m)",
			&_radius, 0.1f, 2000.0f, 0.25f, 1);
		radius->SetPrefabOverrideBinding(GetComponentName(), "/_radius");
		radius->SetOnDrag([this](float value, float, float) { SetRadius(value); });

		// Preview controls - work in edit mode too (the listener follows the
		// scene's main camera), so levels and falloff can be judged in place.
		const int32_t halfWidth = (fullWidth - 6) / 2;
		const Point rowPos = widget->GetNextPos();
		new Button(widget, rowPos, Point(halfWidth, 20), L"Play",
			[this](Button*) { Play(); return true; });
		new Button(widget, Point(rowPos.x + halfWidth + 6, rowPos.y), Point(halfWidth, 20), L"Stop",
			[this](Button*) { Stop(); return true; });

		return true;
	}

	void SoundEffectComponent::OnRenderEditorGizmo(bool isSelected, bool& isHovering)
	{
		(void)isHovering;
		if (!isSelected || !_is3D)
			return;
		if (g_pEnv == nullptr || !g_pEnv->IsEditorMode() || g_pEnv->_debugRenderer == nullptr)
			return;

		// Falloff radius as three great circles around the emitter.
		const math::Vector3 centre = GetEmitterPosition();
		const math::Color colour = IsPlaying()
			? math::Color(0.3f, 1.0f, 0.4f, 0.9f)
			: math::Color(0.3f, 0.8f, 1.0f, 0.7f);

		constexpr int kSegments = 48;
		const float step = 6.28318530718f / (float)kSegments;
		for (int axis = 0; axis < 3; ++axis)
		{
			math::Vector3 prev;
			for (int i = 0; i <= kSegments; ++i)
			{
				const float a = step * (float)i;
				const float c = std::cos(a) * _radius;
				const float s = std::sin(a) * _radius;
				math::Vector3 p;
				switch (axis)
				{
				case 0:  p = centre + math::Vector3(c, s, 0.0f); break;
				case 1:  p = centre + math::Vector3(c, 0.0f, s); break;
				default: p = centre + math::Vector3(0.0f, c, s); break;
				}
				if (i > 0)
					g_pEnv->_debugRenderer->DrawLine(prev, p, colour);
				prev = p;
			}
		}
	}
}
