

#include "SoundEffect.hpp"
#include "../Environment/IEnvironment.hpp"
#include "../FileSystem/ResourceSystem.hpp"

namespace HexEngine
{
	constexpr X3DAUDIO_DISTANCE_CURVE_POINT c_defaultCurvePoints[2] = { { 0.0f, 1.0f }, { 1.0f, 0.0f } };
	constexpr X3DAUDIO_DISTANCE_CURVE c_defaultCurve = { const_cast<X3DAUDIO_DISTANCE_CURVE_POINT*>(c_defaultCurvePoints), 2 };

	SoundEffect::SoundEffect() :
		_wavInfo(nullptr)
	{
		//_emitter.EnableDefaultCurves();	
	}

	std::shared_ptr<SoundEffect> SoundEffect::Create(const fs::path& path)
	{
		return reinterpret_pointer_cast<SoundEffect>(g_pEnv->_resourceSystem->LoadResource(path));
	}
	void SoundEffect::SetVolume(float volume)
	{
		_volume = volume;
		_instance->SetVolume(volume);
	}

	void SoundEffect::SetPitch(float pitch)
	{
		_instance->SetPitch(pitch);
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

	float SoundEffect::GetDuration()
	{
		return (float)(_effect->GetSampleDurationMS() / 1000);
	}

	bool SoundEffect::IsInUse() const
	{
		return _effect->IsInUse();
	}

	bool SoundEffect::IsPlaying() const
	{
		return _instance->GetState() == dx::SoundState::PLAYING;
	}

	float SoundEffect::GetRadius() const
	{
		return _radius;
	}
}