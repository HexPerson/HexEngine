

#include "SoundEffect.hpp"
#include "AudioManager.hpp"
#include "../Environment/IEnvironment.hpp"
#include "../FileSystem/ResourceSystem.hpp"

namespace HexEngine
{
	constexpr X3DAUDIO_DISTANCE_CURVE_POINT c_defaultCurvePoints[2] = { { 0.0f, 1.0f }, { 1.0f, 0.0f } };
	constexpr X3DAUDIO_DISTANCE_CURVE c_defaultCurve = { const_cast<X3DAUDIO_DISTANCE_CURVE_POINT*>(c_defaultCurvePoints), 2 };

	SoundEffect::SoundEffect()
	{
		//_emitter.EnableDefaultCurves();	
	}
#pragma optimize("", off)
	std::shared_ptr<SoundEffect> SoundEffect::Create(const fs::path& path)
	{
		return dynamic_pointer_cast<SoundEffect>(g_pEnv->GetResourceSystem().LoadResource(path));
	}
#pragma optimize("", on)

	std::shared_ptr<SoundEffect> SoundEffect::CreatePlaybackClone() const
	{
		if (_effect == nullptr)
			return nullptr;

		std::shared_ptr<SoundEffect> clone(new SoundEffect(), [](SoundEffect* sound)
			{
				if (sound != nullptr)
				{
					sound->Destroy();
					delete sound;
				}
			});
		clone->_effect = _effect;
		clone->_volume = _volume;
		clone->_emitter = _emitter;
		clone->_is3D = _is3D;
		clone->_radius = _radius;
		clone->_instance = _effect->CreateInstance(dx::SoundEffectInstance_Use3D);
		// Register so the AudioManager's per-frame Apply3D loop picks
		// the clone up. Without this the clone's 3D position is fixed
		// at whatever Loop()/Play() set it to - moving emitters
		// (vehicles, projectiles) appear stuck at their spawn point.
		// Resource-loaded SoundEffects auto-register in LoadResource*().
		if (g_pEnv != nullptr && g_pEnv->_audioManager != nullptr)
			g_pEnv->_audioManager->RegisterPlaybackInstance(clone);
		return clone;
	}

	void SoundEffect::SetVolume(float volume)
	{
		_volume = volume;
		_instance->SetVolume(volume);
	}

	void SoundEffect::SetPitch(float pitch)
	{
		// DirectX SoundEffectInstance::SetPitch is asserts-on-out-of-range
		// in [-1, 1] (semi-octave: -1 = down one octave, +1 = up one
		// octave, 0 = original). Clamp here so a caller passing a stale
		// or unconverted value doesn't crash the audio thread.
		_instance->SetPitch(std::clamp(pitch, -1.0f, 1.0f));
	}

	void SoundEffect::SetRadius(float radius)
	{
		//_emitter.EnableDefaultCurves();

		_emitter.pVolumeCurve = const_cast<X3DAUDIO_DISTANCE_CURVE*>(&c_defaultCurve);
		_emitter.pLFECurve = const_cast<X3DAUDIO_DISTANCE_CURVE*>(&c_defaultCurve);
		_emitter.pLPFDirectCurve = _emitter.pLPFReverbCurve = _emitter.pReverbCurve = nullptr;

		_emitter.CurveDistanceScaler = radius;
		_radius = radius;
	}

	void SoundEffect::SetPosition(const math::Vector3& position)
	{
		// AudioManager::Update() re-applies Apply3D each frame using the
		// emitter's stored position, so updating it here is enough to
		// move a playing/looping sound without a restart.
		_emitter.SetPosition(position);
	}

	float SoundEffect::GetDuration()
	{
		return _effect ? (float)(_effect->GetSampleDurationMS() / 1000) : 0.0f;
	}

	bool SoundEffect::IsInUse() const
	{
		return _effect ? _effect->IsInUse() : false;
	}

	bool SoundEffect::IsPlaying() const
	{
		return _instance && _instance->GetState() == dx::SoundState::PLAYING;
	}

	float SoundEffect::GetRadius() const
	{
		return _radius;
	}
}
