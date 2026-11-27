#include "FootstepComponent.hpp"
#include "SkeletalAnimationComponent.hpp"
#include "RigidBody.hpp"
#include "Camera.hpp"
#include "../Entity.hpp"
#include "../Messaging/Message.hpp"
#include "../../Scene/Scene.hpp"
#include "../../Environment/IEnvironment.hpp"
#include "../../Environment/LogFile.hpp"
#include "../../Audio/AudioManager.hpp"
#include "../../Physics/IPhysicsSystem.hpp"
#include "../../Math/FloatMath.hpp"
#include "../../GUI/Elements/ComponentWidget.hpp"
#include "../../GUI/Elements/DragFloat.hpp"
#include "../../GUI/Elements/LineEdit.hpp"
#include <algorithm>
#include <cmath>

namespace HexEngine
{
	namespace
	{
		// One pool for every character: clones stay alive until they finish
		// (AudioManager only holds weak refs), capped so a crowd can't flood the mixer.
		std::vector<std::shared_ptr<SoundEffect>> s_voices;
		constexpr size_t kMaxVoices = 24;

		std::string Narrow(const std::wstring& w) { return std::string(w.begin(), w.end()); }
		std::wstring Wide(const std::string& s) { return std::wstring(s.begin(), s.end()); }
	}

	FootstepComponent::FootstepComponent(Entity* entity) :
		UpdateComponent(entity)
	{
	}

	FootstepComponent::FootstepComponent(Entity* entity, FootstepComponent* copy) :
		UpdateComponent(entity)
	{
		if (copy != nullptr)
		{
			_leftEvent = copy->_leftEvent;
			_rightEvent = copy->_rightEvent;
			_leftBone = copy->_leftBone;
			_rightBone = copy->_rightBone;
			_hardSounds = copy->_hardSounds;
			_softSounds = copy->_softSounds;
			_softSurfacePrefix = copy->_softSurfacePrefix;
			_volume = copy->_volume;
			_radius = copy->_radius;
			_hearDistance = copy->_hearDistance;
		}
	}

	void FootstepComponent::Update(float frameTime)
	{
		UpdateComponent::Update(frameTime);
	}

	void FootstepComponent::SplitList(const std::string& list, std::vector<std::string>& out)
	{
		out.clear();
		size_t start = 0;
		while (start <= list.size())
		{
			size_t end = list.find(',', start);
			if (end == std::string::npos)
				end = list.size();
			std::string item = list.substr(start, end - start);
			item.erase(0, item.find_first_not_of(" \t"));
			if (const size_t last = item.find_last_not_of(" \t"); last != std::string::npos)
				item.erase(last + 1);
			else
				item.clear();
			if (!item.empty())
				out.push_back(item);
			start = end + 1;
		}
	}

	bool FootstepComponent::LoadSets()
	{
		const std::string key = _hardSounds + "|" + _softSounds;
		if (_loaded && key == _loadedKey)
			return !_hard.empty();

		_loaded = true;
		_loadedKey = key;
		_hard.clear();
		_soft.clear();
		std::vector<std::string> paths;
		SplitList(_hardSounds, paths);
		for (const auto& p : paths)
			if (auto s = SoundEffect::Create(p))
				_hard.push_back(s);
		SplitList(_softSounds, paths);
		for (const auto& p : paths)
			if (auto s = SoundEffect::Create(p))
				_soft.push_back(s);
		if (_hard.empty() && !_soft.empty())
			_hard = _soft;
		if (_soft.empty())
			_soft = _hard;
		if (_hard.empty())
			LOG_WARN("FootstepComponent on '%s': no footstep sounds could be loaded", GetEntity()->GetName().c_str());
		return !_hard.empty();
	}

	math::Vector3 FootstepComponent::FootPosition(bool left)
	{
		Entity* e = GetEntity();
		const math::Matrix world = e->GetWorldTM();
		if (auto* skel = e->GetComponent<SkeletalAnimationComponent>(); skel != nullptr && skel->GetBoundMesh() != nullptr)
		{
			if (BoneInfo* bi = skel->GetBoneInfoByName(left ? _leftBone : _rightBone); bi != nullptr)
			{
				const math::Vector3 p = math::Vector3::Transform(bi->Position, world);
				// Sanity: a foot is within a couple of metres of its owner.
				if (std::isfinite(p.x) && (p - world.Translation()).LengthSquared() < 4.0f)
					return p;
			}
		}
		return world.Translation();
	}

	void FootstepComponent::PlayStep(bool left)
	{
		if (g_pEnv == nullptr || g_pEnv->_audioManager == nullptr || g_pEnv->_physicsSystem == nullptr || !LoadSets())
			return;

		Entity* e = GetEntity();
		Scene* scene = e->GetScene();
		if (scene == nullptr)
			return;

		// Out of earshot of the listener (the main camera): skip the work.
		if (Camera* cam = scene->GetMainCamera(); cam != nullptr && cam->GetEntity() != nullptr)
		{
			const math::Vector3 ear = cam->GetEntity()->GetWorldTM().Translation();
			if (math::Vector3::DistanceSquared(ear, e->GetWorldTM().Translation()) > _hearDistance * _hearDistance)
				return;
		}

		// What's under this foot? Nothing within reach = airborne: no step.
		const math::Vector3 foot = FootPosition(left);
		const math::Vector3 from(foot.x, e->GetWorldTM().Translation().y + 0.6f, foot.z);
		IRigidBody* ignore = nullptr;
		if (auto* rb = e->GetComponent<RigidBody>(); rb != nullptr)
			ignore = rb->GetIRigidBody();
		RayHit hit;
		if (g_pEnv->_physicsSystem->RayCastScene(from, math::Vector3(0.0f, -1.0f, 0.0f), 2.0f, &hit, ignore) == 0)
			return;

		const bool soft = !_softSurfacePrefix.empty() && hit.entity != nullptr &&
			hit.entity->GetName().rfind(_softSurfacePrefix, 0) == 0;
		auto& set = soft ? _soft : _hard;
		if (set.empty())
			return;

		s_voices.erase(std::remove_if(s_voices.begin(), s_voices.end(),
			[](const std::shared_ptr<SoundEffect>& s) { return s == nullptr || !s->IsPlaying(); }), s_voices.end());
		if (s_voices.size() >= kMaxVoices)
			return;

		int32_t idx = (int32_t)GetRandomFloat(0.0f, (float)set.size() - 0.001f);
		if (set.size() > 1 && idx == _lastIndex)
			idx = (idx + 1) % (int32_t)set.size();
		_lastIndex = idx;

		auto voice = set[idx]->CreatePlaybackClone();
		if (voice == nullptr)
			return;
		voice->SetVolume(std::clamp(_volume, 0.0f, 1.0f));
		voice->SetRadius(std::max(_radius, 0.5f));
		voice->SetPitch(GetRandomFloat(-0.08f, 0.08f) + (left ? -0.02f : 0.02f));
		g_pEnv->_audioManager->Play(voice, hit.position);
		s_voices.push_back(std::move(voice));

		LOG_DEBUG("Footstep: %s %s on %s (%s)", e->GetName().c_str(), left ? "L" : "R",
			hit.entity != nullptr ? hit.entity->GetName().c_str() : "?", soft ? "soft" : "hard");
	}

	void FootstepComponent::OnMessage(Message* message, MessageListener* sender)
	{
		UpdateComponent::OnMessage(message, sender);
		if (message == nullptr || message->_id != MessageId::AnimationEvent)
			return;
		if (g_pEnv == nullptr || !g_pEnv->IsGameRunning())
			return;
		if (GetEntity() == nullptr || GetEntity()->HasFlag(EntityFlags::DoNotRender))
			return;   // hidden: e.g. a resident driving their car

		const auto* ev = message->CastAs<AnimationEventMessage>();
		if (ev == nullptr)
			return;
		if (ev->eventName == _leftEvent)
			PlayStep(true);
		else if (ev->eventName == _rightEvent)
			PlayStep(false);
	}

	void FootstepComponent::Serialize(json& data, JsonFile* file)
	{
		SERIALIZE_VALUE(_leftEvent);
		SERIALIZE_VALUE(_rightEvent);
		SERIALIZE_VALUE(_leftBone);
		SERIALIZE_VALUE(_rightBone);
		SERIALIZE_VALUE(_hardSounds);
		SERIALIZE_VALUE(_softSounds);
		SERIALIZE_VALUE(_softSurfacePrefix);
		SERIALIZE_VALUE(_volume);
		SERIALIZE_VALUE(_radius);
		SERIALIZE_VALUE(_hearDistance);
	}

	void FootstepComponent::Deserialize(json& data, JsonFile* file, uint32_t mask)
	{
		(void)mask;
		DESERIALIZE_VALUE(_leftEvent);
		DESERIALIZE_VALUE(_rightEvent);
		DESERIALIZE_VALUE(_leftBone);
		DESERIALIZE_VALUE(_rightBone);
		DESERIALIZE_VALUE(_hardSounds);
		DESERIALIZE_VALUE(_softSounds);
		DESERIALIZE_VALUE(_softSurfacePrefix);
		DESERIALIZE_VALUE(_volume);
		DESERIALIZE_VALUE(_radius);
		DESERIALIZE_VALUE(_hearDistance);
		_volume = std::clamp(_volume, 0.0f, 1.0f);
		_radius = std::max(_radius, 0.5f);
		_hearDistance = std::max(_hearDistance, 1.0f);
		_loaded = false;   // sound lists may have changed
	}

	bool FootstepComponent::CreateWidget(ComponentWidget* widget)
	{
		const int32_t w = widget->GetSize().x - 20;
		const auto text = [&](const wchar_t* label, std::string* field, const char* binding)
		{
			auto* edit = new LineEdit(widget, widget->GetNextPos(), Point(w, 18), label);
			edit->SetValue(Wide(*field));
			edit->SetPrefabOverrideBinding(GetComponentName(), binding);
			edit->SetOnInputFn([this, field](LineEdit*, const std::wstring& v) { *field = Narrow(v); _loaded = false; });
		};
		text(L"Left foot event", &_leftEvent, "/_leftEvent");
		text(L"Right foot event", &_rightEvent, "/_rightEvent");
		text(L"Left foot bone", &_leftBone, "/_leftBone");
		text(L"Right foot bone", &_rightBone, "/_rightBone");
		text(L"Hard sounds (a, b, ...)", &_hardSounds, "/_hardSounds");
		text(L"Soft sounds (a, b, ...)", &_softSounds, "/_softSounds");
		text(L"Soft surface name prefix", &_softSurfacePrefix, "/_softSurfacePrefix");

		auto* vol = new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Volume", &_volume, 0.0f, 1.0f, 0.01f, 2);
		vol->SetPrefabOverrideBinding(GetComponentName(), "/_volume");
		auto* rad = new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Radius (m)", &_radius, 0.5f, 200.0f, 0.25f, 1);
		rad->SetPrefabOverrideBinding(GetComponentName(), "/_radius");
		auto* hear = new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Hear distance (m)", &_hearDistance, 1.0f, 500.0f, 0.5f, 1);
		hear->SetPrefabOverrideBinding(GetComponentName(), "/_hearDistance");
		return true;
	}
}
