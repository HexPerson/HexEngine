

#include "DoorComponent.hpp"

#include "../Entity.hpp"
#include "Transform.hpp"
#include "StaticMeshComponent.hpp"
#include "../../Environment/IEnvironment.hpp"
#include "../../Graphics/DebugRenderer.hpp"
#include "../../Audio/AudioManager.hpp"
#include "../../Audio/SoundEffect.hpp"
#include "../../Math/easing.h"
#include "../../GUI/Elements/ComponentWidget.hpp"
#include "../../GUI/Elements/DragFloat.hpp"
#include "../../GUI/Elements/Checkbox.hpp"
#include "../../GUI/Elements/DropDown.hpp"
#include "../../GUI/Elements/AssetSearch.hpp"
#include <algorithm>
#include <cmath>

namespace HexEngine
{
	DoorComponent::DoorComponent(Entity* entity) :
		InteractionComponent(entity)
	{}

	DoorComponent::DoorComponent(Entity* entity, DoorComponent* clone) :
		InteractionComponent(entity, clone) // copies the interaction fields (name, range, glow, key, ...)
	{
		if (clone == nullptr)
			return;
		_openAngle      = clone->_openAngle;
		_swingSpeed     = clone->_swingSpeed;
		_startOpen      = clone->_startOpen;
		_soundVolume    = clone->_soundVolume;
		_hingeOffset    = clone->_hingeOffset;
		_openSoundPath  = clone->_openSoundPath;
		_closeSoundPath = clone->_closeSoundPath;
		// Runtime state (angle / captured rotation / sounds) is intentionally not
		// copied - it re-initialises when play starts.
	}

	void DoorComponent::Update(float frameTime)
	{
		// Base tick resolves look-at focus (drives the glow + on-screen label).
		InteractionComponent::Update(frameTime);

		// Only swing while the game is running. In the editor we stay inert so the
		// authored door rotation isn't fought, and we re-capture the closed
		// orientation each time play begins.
		if (g_pEnv == nullptr || !g_pEnv->IsGameRunning())
		{
			// Reset runtime swing state so the next run re-captures from the authored
			// pose. The transform itself is restored scene-wide by GameIntegrator's
			// play/stop snapshot, so we don't touch it here.
			SetSwingGiExclusion(false);
			_isOpen = false;
			_t = 0.0f;
			_closedCaptured = false;
			return;
		}

		auto* tf = GetEntity()->GetComponent<Transform>();
		if (tf == nullptr)
			return;

		if (!_closedCaptured)
		{
			_closedPosition = tf->GetPosition();
			_closedRotation = tf->GetRotation();
			// Fully-open orientation, used as the slerp endpoint.
			const float openRad = _openAngle * 0.01745329252f;
			_openRotation = math::Quaternion::CreateFromAxisAngle(math::Vector3::Up, openRad) * _closedRotation;
			_openRotation.Normalize();
			_closedCaptured = true;
			_isOpen = _startOpen;
			_t = _startOpen ? 1.0f : 0.0f;
			ApplyAngle();
			return;
		}

		const float target = _isOpen ? 1.0f : 0.0f;
		if (std::fabs(_t - target) > 1e-4f)
		{
			// Pull the door out of GI while it's moving (per-frame re-voxelisation
			// is what makes the swing chug); restored when it settles below.
			SetSwingGiExclusion(true);

			// Time-normalised progress so we can ease it. Duration derives from the
			// authored swing speed (deg/s) over the full open angle.
			const float duration = std::max(0.05f, std::fabs(_openAngle) / std::max(1.0f, _swingSpeed));
			const float dir = _isOpen ? 1.0f : -1.0f;
			_t = std::clamp(_t + dir * (frameTime / duration), 0.0f, 1.0f);
			ApplyAngle();

			if (std::fabs(_t - target) <= 1e-4f)
				SetSwingGiExclusion(false); // settled - hand the door back to GI at its final pose
		}
	}

	void DoorComponent::ApplyAngle()
	{
		auto* tf = GetEntity()->GetComponent<Transform>();
		if (tf == nullptr)
			return;

		// Ease the linear progress with the engine's easing functions (Math/easing.h)
		// so the swing accelerates/decelerates per the chosen curve. _easing == -1
		// (or an unknown value) falls back to linear.
		float eased = _t;
		if (auto fn = getEasingFunction(static_cast<easing_functions>(_easing)); fn != nullptr)
			eased = static_cast<float>(fn(static_cast<double>(_t)));

		// Rotation: slerp between the captured closed and fully-open orientations.
		math::Quaternion newRot = math::Quaternion::Slerp(_closedRotation, _openRotation, eased);
		newRot.Normalize();

		// Position: orbit the origin around the hinge by the eased angle (a true
		// arc - lerping closed->open positions would cut the chord and not pivot).
		const float easedRad = _openAngle * eased * 0.01745329252f;
		const math::Quaternion swing = math::Quaternion::CreateFromAxisAngle(math::Vector3::Up, easedRad);
		const math::Vector3 scaledOffset = _hingeOffset * tf->GetScale();
		const math::Vector3 hinge = _closedPosition + math::Vector3::Transform(scaledOffset, _closedRotation);
		const math::Vector3 newPos = hinge + math::Vector3::Transform(_closedPosition - hinge, swing);

		tf->SetRotation(newRot);
		tf->SetPosition(newPos);
	}

	void DoorComponent::SetSwingGiExclusion(bool excluded)
	{
		auto* smc = GetEntity()->GetComponent<StaticMeshComponent>();
		if (smc == nullptr)
			return;

		if (excluded)
		{
			if (!_giExcludedForSwing)
			{
				_meshOriginalExclude = smc->GetExcludeFromGI();
				if (!_meshOriginalExclude)
					smc->SetExcludeFromGI(true);
				_giExcludedForSwing = true;
			}
		}
		else if (_giExcludedForSwing)
		{
			smc->SetExcludeFromGI(_meshOriginalExclude); // restore authored GI state at the settled pose
			_giExcludedForSwing = false;
		}
	}

	void DoorComponent::Callback()
	{
		// Fire any extra callback a designer wired via SetCallback, then toggle.
		InteractionComponent::Callback();
		Toggle();
	}

	void DoorComponent::Open()
	{
		if (_isOpen)
			return;
		_isOpen = true;
		PlayDoorSound(_openSoundPath, _openSound);
	}

	void DoorComponent::Close()
	{
		if (!_isOpen)
			return;
		_isOpen = false;
		PlayDoorSound(_closeSoundPath, _closeSound);
	}

	void DoorComponent::Toggle()
	{
		if (_isOpen)
			Close();
		else
			Open();
	}

	void DoorComponent::PlayDoorSound(const std::string& path, std::shared_ptr<SoundEffect>& master)
	{
		if (path.empty() || g_pEnv == nullptr || g_pEnv->_audioManager == nullptr)
			return;

		// Drop finished clones (AudioManager holds only weak refs, so we keep them
		// alive here until they finish).
		_activeSounds.erase(
			std::remove_if(_activeSounds.begin(), _activeSounds.end(),
				[](const std::shared_ptr<SoundEffect>& s) { return s == nullptr || !s->IsPlaying(); }),
			_activeSounds.end());

		if (master == nullptr)
			master = SoundEffect::Create(path);
		if (master == nullptr)
			return;

		auto clone = master->CreatePlaybackClone();
		if (clone == nullptr)
			return;
		clone->SetVolume(_soundVolume);

		// Positional (3D) playback at the door so it attenuates with distance.
		if (auto* tf = GetEntity()->GetComponent<Transform>(); tf != nullptr)
			g_pEnv->_audioManager->Play(clone, tf->GetPosition());
		else
			g_pEnv->_audioManager->Play(clone);

		_activeSounds.push_back(std::move(clone));
	}

	void DoorComponent::Serialize(json& data, JsonFile* file)
	{
		InteractionComponent::Serialize(data, file);
		SERIALIZE_VALUE(_openAngle);
		SERIALIZE_VALUE(_swingSpeed);
		SERIALIZE_VALUE(_easing);
		SERIALIZE_VALUE(_startOpen);
		SERIALIZE_VALUE(_soundVolume);
		SERIALIZE_VALUE(_hingeOffset);
		SERIALIZE_VALUE(_openSoundPath);
		SERIALIZE_VALUE(_closeSoundPath);
	}

	void DoorComponent::Deserialize(json& data, JsonFile* file, uint32_t mask)
	{
		InteractionComponent::Deserialize(data, file, mask);
		DESERIALIZE_VALUE(_openAngle);
		DESERIALIZE_VALUE(_swingSpeed);
		DESERIALIZE_VALUE(_easing);
		DESERIALIZE_VALUE(_startOpen);
		DESERIALIZE_VALUE(_soundVolume);
		DESERIALIZE_VALUE(_hingeOffset);
		DESERIALIZE_VALUE(_openSoundPath);
		DESERIALIZE_VALUE(_closeSoundPath);
	}

	void DoorComponent::OnRenderEditorGizmo(bool isSelected, bool& isHovering)
	{
		(void)isHovering;
		if (g_pEnv == nullptr || !g_pEnv->IsEditorMode() || g_pEnv->_debugRenderer == nullptr)
			return;

		auto* ent = GetEntity();
		if (ent == nullptr)
			return;

		// Hinge point in world space (the local offset through the full world TM,
		// so it tracks the door's authored position / rotation / scale).
		const math::Matrix world = ent->GetWorldTM();
		const math::Vector3 hinge = math::Vector3::Transform(_hingeOffset, world);
		math::Vector3 up = world.Up();
		if (up.LengthSquared() > 0.0f)
			up.Normalize();
		else
			up = math::Vector3::Up;

		const math::Color colour = isSelected
			? math::Color(1.0f, 0.85f, 0.2f, 1.0f)
			: math::Color(0.9f, 0.7f, 0.1f, 0.5f);

		// Vertical hinge axis through the pivot + a small marker box at it.
		g_pEnv->_debugRenderer->DrawLine(hinge - up * 1.2f, hinge + up * 1.2f, colour);
		dx::BoundingBox marker;
		marker.Center = hinge;
		marker.Extents = math::Vector3(0.06f, 0.06f, 0.06f);
		g_pEnv->_debugRenderer->DrawAABB(marker, colour);
	}

	bool DoorComponent::CreateWidget(ComponentWidget* widget)
	{
		// Interaction fields (name, range, glow colour, outline, enabled) first.
		InteractionComponent::CreateWidget(widget);

		const int32_t fullWidth = widget->GetSize().x - 20;

		new DragFloat(widget, widget->GetNextPos(), Point(fullWidth, 18), L"Open Angle (deg)",
			&_openAngle, -180.0f, 180.0f, 1.0f, 1);
		new DragFloat(widget, widget->GetNextPos(), Point(fullWidth, 18), L"Swing Speed (deg/s)",
			&_swingSpeed, 1.0f, 1000.0f, 1.0f, 0);

		// Easing curve, from the engine's Math/easing.h set. -1 = linear.
		auto* easeDrop = new DropDown(widget, widget->GetNextPos(), Point(200, 18), L"Easing");
		easeDrop->GetContextMenu()->AddItem(new ContextItem(L"Linear",
			[this](const std::wstring&) { _easing = -1; }));
		static const wchar_t* kEaseNames[] = {
			L"In Sine", L"Out Sine", L"InOut Sine",
			L"In Quad", L"Out Quad", L"InOut Quad",
			L"In Cubic", L"Out Cubic", L"InOut Cubic",
			L"In Quart", L"Out Quart", L"InOut Quart",
			L"In Quint", L"Out Quint", L"InOut Quint",
			L"In Expo", L"Out Expo", L"InOut Expo",
			L"In Circ", L"Out Circ", L"InOut Circ",
			L"In Back", L"Out Back", L"InOut Back",
			L"In Elastic", L"Out Elastic", L"InOut Elastic",
			L"In Bounce", L"Out Bounce", L"InOut Bounce",
		};
		for (int32_t i = 0; i < (int32_t)(sizeof(kEaseNames) / sizeof(kEaseNames[0])); ++i)
			easeDrop->GetContextMenu()->AddItem(new ContextItem(kEaseNames[i],
				[this, i](const std::wstring&) { _easing = i; }));

		new Checkbox(widget, widget->GetNextPos(), Point(fullWidth, 18), L"Start Open", &_startOpen);
		new DragFloat(widget, widget->GetNextPos(), Point(fullWidth, 18), L"Sound Volume",
			&_soundVolume, 0.0f, 1.0f, 0.01f, 2);

		// Hinge pivot (local space, origin -> hinge edge). The yellow gizmo marker
		// shows where it currently sits; drag these until it's on the door's hinge.
		new DragFloat(widget, widget->GetNextPos(), Point(fullWidth, 18), L"Hinge Offset X",
			&_hingeOffset.x, -1000.0f, 1000.0f, 0.01f, 3);
		new DragFloat(widget, widget->GetNextPos(), Point(fullWidth, 18), L"Hinge Offset Y",
			&_hingeOffset.y, -1000.0f, 1000.0f, 0.01f, 3);
		new DragFloat(widget, widget->GetNextPos(), Point(fullWidth, 18), L"Hinge Offset Z",
			&_hingeOffset.z, -1000.0f, 1000.0f, 0.01f, 3);

		auto* openSearch = new AssetSearch(widget, widget->GetNextPos(), Point(fullWidth, 84),
			L"Open Sound", { ResourceType::Audio },
			[this](AssetSearch*, const AssetSearchResult& result)
			{
				const fs::path& chosen = !result.assetPath.empty() ? result.assetPath : result.absolutePath;
				_openSoundPath = chosen.string();
				_openSound = nullptr; // reload on next play
			});
		if (!_openSoundPath.empty())
			openSearch->SetValue(std::wstring(_openSoundPath.begin(), _openSoundPath.end()));

		auto* closeSearch = new AssetSearch(widget, widget->GetNextPos(), Point(fullWidth, 84),
			L"Close Sound", { ResourceType::Audio },
			[this](AssetSearch*, const AssetSearchResult& result)
			{
				const fs::path& chosen = !result.assetPath.empty() ? result.assetPath : result.absolutePath;
				_closeSoundPath = chosen.string();
				_closeSound = nullptr;
			});
		if (!_closeSoundPath.empty())
			closeSearch->SetValue(std::wstring(_closeSoundPath.begin(), _closeSoundPath.end()));

		return true;
	}
}
