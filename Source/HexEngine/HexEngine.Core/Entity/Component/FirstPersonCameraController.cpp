

#include "FirstPersonCameraController.hpp"
#include "RigidBody.hpp"
#include "../../Environment/IEnvironment.hpp"
#include "../../Input/InputSystem.hpp"
#include "../Entity.hpp"
#include "../Component/Camera.hpp"
#include "../../Environment/TimeManager.hpp"
#include "../../Scene/SceneManager.hpp"
#include "../../Input/CommandManager.hpp"
#include "../../Input/HCommand.hpp"
#include "../../Audio/AudioManager.hpp"
#include "../../Audio/SoundEffect.hpp"
#include "../../Math/FloatMath.hpp"
#include "../../Physics/PhysUtils.hpp"
#include "../../Physics/IPhysicsSystem.hpp"
#include "../../Graphics/Material.hpp"
#include "../../GUI/Elements/ComponentWidget.hpp"
#include "../../GUI/Elements/DragFloat.hpp"
#include "../../GUI/Elements/AssetSearch.hpp"
#include "../../Graphics/ITexture2D.hpp"
#include <cfloat>
#include <cmath>
#include <dxgiformat.h>
#include <algorithm>

namespace HexEngine
{
	HEX_COMMAND(MoveForwards)
	{
		auto controller = reinterpret_cast<FirstPersonCameraController*>(param);

		if (pressed)
			controller->AddInputFlag(MoveFlag::MoveForwards);
		else
			controller->RemoveInputFlag(MoveFlag::MoveForwards);
	}

	HEX_COMMAND(MoveBackwards)
	{
		auto controller = reinterpret_cast<FirstPersonCameraController*>(param);

		if (pressed)
			controller->AddInputFlag(MoveFlag::MoveBackwards);
		else
			controller->RemoveInputFlag(MoveFlag::MoveBackwards);
	}

	HEX_COMMAND(MoveLeft)
	{
		auto controller = reinterpret_cast<FirstPersonCameraController*>(param);

		if (pressed)
			controller->AddInputFlag(MoveFlag::MoveLeft);
		else
			controller->RemoveInputFlag(MoveFlag::MoveLeft);
	}

	HEX_COMMAND(MoveRight)
	{
		auto controller = reinterpret_cast<FirstPersonCameraController*>(param);

		if (pressed)
			controller->AddInputFlag(MoveFlag::MoveRight);
		else
			controller->RemoveInputFlag(MoveFlag::MoveRight);
	}

	HEX_COMMAND(MoveUp)
	{
		auto controller = reinterpret_cast<FirstPersonCameraController*>(param);

		if (pressed)
			controller->AddInputFlag(MoveFlag::MoveUp);
		else
			controller->RemoveInputFlag(MoveFlag::MoveUp);
	}

	HEX_COMMAND(MoveDown)
	{
		auto controller = reinterpret_cast<FirstPersonCameraController*>(param);

		if (pressed)
			controller->AddInputFlag(MoveFlag::MoveDown);
		else
			controller->RemoveInputFlag(MoveFlag::MoveDown);
	}

	HEX_COMMAND(MoveRun)
	{
		auto controller = reinterpret_cast<FirstPersonCameraController*>(param);

		if (pressed)
			controller->AddInputFlag(MoveFlag::MoveSprint);
		else
			controller->RemoveInputFlag(MoveFlag::MoveSprint);
	}

	FirstPersonCameraController::FirstPersonCameraController(Entity* entity) :
		UpdateComponent(entity)
	{
		if (auto* transform = entity->GetComponent<Transform>())
			transform->EnableInterpolation(true);

		g_pEnv->_inputSystem->AddInputListener(this, InputEvent::KeyDown | InputEvent::KeyUp | InputEvent::MouseDown | InputEvent::MouseUp | InputEvent::MouseWheel | InputEvent::MouseMove);

		g_pEnv->_commandManager->CreateBind('W', "MoveForwards", this);
		g_pEnv->_commandManager->CreateBind('S', "MoveBackwards", this);
		g_pEnv->_commandManager->CreateBind('A', "MoveLeft", this);
		g_pEnv->_commandManager->CreateBind('D', "MoveRight", this);
		g_pEnv->_commandManager->CreateBind(VK_SPACE, "MoveUp", this);
		g_pEnv->_commandManager->CreateBind(VK_CONTROL, "MoveDown", this);
		g_pEnv->_commandManager->CreateBind(VK_SHIFT, "MoveRun", this);
	}

	FirstPersonCameraController::FirstPersonCameraController(Entity* entity, FirstPersonCameraController* clone) :
		UpdateComponent(entity)
	{
		if (clone != nullptr)
		{
			_footstepSoundPath      = clone->_footstepSoundPath;
			_strideLength           = clone->_strideLength;
			_footstepVolume         = clone->_footstepVolume;
			_footstepPitchVariation = clone->_footstepPitchVariation;
		}

		if (auto* transform = entity->GetComponent<Transform>())
			transform->EnableInterpolation(true);

		g_pEnv->_inputSystem->AddInputListener(this, InputEvent::KeyDown | InputEvent::KeyUp | InputEvent::MouseDown | InputEvent::MouseUp | InputEvent::MouseWheel | InputEvent::MouseMove);

		g_pEnv->_commandManager->CreateBind('W', "MoveForwards", this);
		g_pEnv->_commandManager->CreateBind('S', "MoveBackwards", this);
		g_pEnv->_commandManager->CreateBind('A', "MoveLeft", this);
		g_pEnv->_commandManager->CreateBind('D', "MoveRight", this);
		g_pEnv->_commandManager->CreateBind(VK_SPACE, "MoveUp", this);
		g_pEnv->_commandManager->CreateBind(VK_CONTROL, "MoveDown", this);
		g_pEnv->_commandManager->CreateBind(VK_SHIFT, "MoveRun", this);
	}

	FirstPersonCameraController::~FirstPersonCameraController()
	{
		g_pEnv->_inputSystem->RemoveInputListener(this);
	}

	void FirstPersonCameraController::Update(float frameTime)
	{
		(void)frameTime;
		PruneFinishedFootsteps();
	}

	void FirstPersonCameraController::FixedUpdate(float frameTime)
	{
		if (frameTime <= 0.0f)
			return;

		auto* transform = GetEntity()->GetComponent<Transform>();
		auto* rigidBody = GetEntity()->GetComponent<RigidBody>();
		auto* bodyController = rigidBody ? rigidBody->GetIRigidBody() : nullptr;

		if (!transform || !bodyController)
			return;

		math::Vector3 planarRight = transform->GetRight();
		planarRight.y = 0.0f;

		if (planarRight.Length() <= FLT_EPSILON)
			planarRight = math::Vector3::Right;
		else
			planarRight.Normalize();

		math::Vector3 planarForward = -planarRight.Cross(math::Vector3::Up);

		if (planarForward.Length() <= FLT_EPSILON)
			planarForward = math::Vector3::Forward;
		else
			planarForward.Normalize();

		math::Vector3 planarVelocity;

		if ((_flags & MoveFlag::MoveForwards) != 0)
		{
			bool run = (_flags & MoveFlag::MoveSprint) != 0;
			const float moveSpeed = run ? _movementSpeed * _runMultiplier : _movementSpeed;

			planarVelocity += planarForward * moveSpeed;
		}

		if ((_flags & MoveFlag::MoveBackwards) != 0)
			planarVelocity -= planarForward * _movementSpeed;

		if ((_flags & MoveFlag::MoveRight) != 0)
			planarVelocity += planarRight * _strafeMovementSpeed;

		if ((_flags & MoveFlag::MoveLeft) != 0)
			planarVelocity -= planarRight * _strafeMovementSpeed;

		// Ground stick: when grounded, bias the vertical velocity slightly DOWN
		// rather than zeroing it. A zero-vertical Move produces no DOWN collision,
		// so PhysX reports not-grounded the next tick - IsOnGround then flickers
		// 1/0 every frame, gravity accumulates unbounded while walking, and the
		// capsule tunnels through the floor on movement. The small downward bias
		// keeps the capsule pressed into the floor so ground contact stays
		// detected. Released while flying up (MoveUp) so lift-off still works.
		const float kGroundStickSpeed = 4.0f;
		const bool wantsUp = (_flags & MoveFlag::MoveUp) != 0;
		const bool isOnGround = bodyController->IsOnGround();
		if (isOnGround)
			_verticalVelocity = wantsUp ? 0.0f : -kGroundStickSpeed;
		else
			_verticalVelocity -= _gravityAcceleration * frameTime;

		float manualVerticalVelocity = 0.0f;
		if ((_flags & MoveFlag::MoveUp) != 0)
			manualVerticalVelocity += _verticalMovementSpeed;

		if ((_flags & MoveFlag::MoveDown) != 0)
			manualVerticalVelocity -= _verticalMovementSpeed;

		math::Vector3 displacement = (planarVelocity + (math::Vector3::Up * (_verticalVelocity + manualVerticalVelocity))) * frameTime;
		bodyController->Move(displacement, 0.0f, frameTime);

		// Footsteps: accumulate grounded horizontal distance and emit a step
		// every stride length. Distance-based cadence means sprinting (which
		// covers more ground per second) naturally produces faster footfalls.
		const float planarSpeed = planarVelocity.Length();
		const float stride = std::max(0.1f, _strideLength);
		if (isOnGround && planarSpeed > 0.1f)
		{
			_distanceSinceStep += planarSpeed * frameTime;
			if (_distanceSinceStep >= stride)
			{
				_distanceSinceStep = 0.0f;
				PlayFootstep();
			}
		}
		else
		{
			// Prime so the first step fires right as walking resumes / after
			// landing, instead of leaving a stride-length silent gap.
			_distanceSinceStep = stride;
		}
	}

	void FirstPersonCameraController::AddInputFlag(MoveFlag flag)
	{
		_flags |= flag;
	}

	void FirstPersonCameraController::RemoveInputFlag(MoveFlag flag)
	{
		_flags &= ~flag;
	}

	void FirstPersonCameraController::PlayFootstep()
	{
		if (g_pEnv->_audioManager == nullptr)
			return;

		// Resolve which sound to play: raycast straight down from the player to
		// find the surface under it; if that surface's material specifies its own
		// footstep sound, use it, otherwise fall back to the controller default.
		std::string soundPath = _footstepSoundPath;
		if (auto* tf = GetEntity()->GetComponent<Transform>(); tf != nullptr)
		{
			const math::Vector3 origin = tf->GetPosition();
			math::Ray ray(origin, math::Vector3(0.0f, -1.0f, 0.0f));
			RayHit hit;
			const std::vector<Entity*> ignore{ GetEntity() };
			if (PhysUtils::RayCast(ray, std::max(0.1f, _footstepRayDistance),
				LAYERMASK(Layer::StaticGeometry) | LAYERMASK(Layer::DynamicGeometry), &hit, ignore))
			{
				if (auto mat = hit.material.lock(); mat != nullptr)
				{
					// Precedence: a per-region surface-ID map sound (atlas / splatmap
					// materials) wins; otherwise the material's single footstep sound;
					// otherwise the controller default already in soundPath.
					std::string resolved;
					const std::string& surfaceMap = mat->GetFootstepSurfaceMapPath();
					if (!surfaceMap.empty())
					{
						const int32_t id = SampleFootstepSurfaceId(surfaceMap, hit.uv.x, hit.uv.y);
						if (id >= 0)
						{
							const std::string& s = mat->GetFootstepSurfaceSound(id);
							if (!s.empty())
								resolved = s;
						}
					}
					if (resolved.empty() && !mat->GetFootstepSoundPath().empty())
						resolved = mat->GetFootstepSoundPath();
					if (!resolved.empty())
						soundPath = resolved;
				}
			}
		}
		if (soundPath.empty())
			return;

		// Lazily load + cache the master sound for this path (the default and
		// each material's footstep sound each get their own cached entry).
		auto& master = _footstepSoundCache[soundPath];
		if (master == nullptr)
			master = SoundEffect::Create(soundPath);
		if (master == nullptr)
			return;

		// Fresh instance per step so rapid footfalls overlap cleanly.
		auto step = master->CreatePlaybackClone();
		if (step == nullptr)
			return;
		step->SetVolume(_footstepVolume);
		// Small per-step pitch jitter so repeated steps don't sound identical.
		step->SetPitch(GetRandomFloat(-_footstepPitchVariation, _footstepPitchVariation));
		// 2D: the listener IS the player, so the player's own footsteps play
		// centred at full volume - no 3D positioning needed.
		g_pEnv->_audioManager->Play(step);
		// AudioManager keeps only a weak ref; hold the clone until it finishes
		// (PruneFinishedFootsteps drops it once IsPlaying() goes false).
		_activeFootsteps.push_back(std::move(step));
	}

	int32_t FirstPersonCameraController::SampleFootstepSurfaceId(const std::string& mapPath, float u, float v)
	{
		// Lazily decode the surface-ID map to CPU pixels once and cache them. The
		// map's RED channel encodes the surface id; we nearest-sample at the hit UV.
		// (Mirrors the GI material-texel CPU read in DiffuseGI.)
		auto& m = _footstepSurfaceMapCache[mapPath];
		if (!m.loaded)
		{
			m.loaded = true;
			if (auto tex = ITexture2D::Create(mapPath); tex != nullptr)
			{
				m.width = std::max(1, tex->GetWidth());
				m.height = std::max(1, tex->GetHeight());
				tex->GetPixels(m.pixels);
				const size_t tight = static_cast<size_t>(m.width) * static_cast<size_t>(m.height) * 4u;
				if (m.pixels.size() >= tight)
				{
					const uint32_t fmt = tex->GetFormat();
					m.isBgra =
						(fmt == DXGI_FORMAT_B8G8R8A8_UNORM) ||
						(fmt == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) ||
						(fmt == DXGI_FORMAT_B8G8R8X8_UNORM) ||
						(fmt == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB);
					m.valid = true;
				}
			}
		}
		if (!m.valid)
			return -1;

		u = u - std::floor(u);
		v = v - std::floor(v);
		if (u < 0.0f) u += 1.0f;
		if (v < 0.0f) v += 1.0f;

		const int32_t x = std::clamp(static_cast<int32_t>(u * static_cast<float>(m.width - 1) + 0.5f), 0, m.width - 1);
		const int32_t y = std::clamp(static_cast<int32_t>(v * static_cast<float>(m.height - 1) + 0.5f), 0, m.height - 1);
		const size_t idx = (static_cast<size_t>(y) * static_cast<size_t>(m.width) + static_cast<size_t>(x)) * 4u;
		if (idx + 3u >= m.pixels.size())
			return -1;
		const size_t cR = m.isBgra ? 2u : 0u;
		return static_cast<int32_t>(m.pixels[idx + cR]);
	}

	void FirstPersonCameraController::PruneFinishedFootsteps()
	{
		if (_activeFootsteps.empty())
			return;
		_activeFootsteps.erase(
			std::remove_if(_activeFootsteps.begin(), _activeFootsteps.end(),
				[](const std::shared_ptr<SoundEffect>& s) { return !s || !s->IsPlaying(); }),
			_activeFootsteps.end());
	}

	void FirstPersonCameraController::Serialize(json& data, JsonFile* file)
	{
		SERIALIZE_VALUE(_footstepSoundPath);
		SERIALIZE_VALUE(_strideLength);
		SERIALIZE_VALUE(_footstepVolume);
		SERIALIZE_VALUE(_footstepPitchVariation);
	}

	void FirstPersonCameraController::Deserialize(json& data, JsonFile* file, uint32_t mask)
	{
		DESERIALIZE_VALUE(_footstepSoundPath);
		DESERIALIZE_VALUE(_strideLength);
		DESERIALIZE_VALUE(_footstepVolume);
		DESERIALIZE_VALUE(_footstepPitchVariation);
	}

	bool FirstPersonCameraController::CreateWidget(ComponentWidget* widget)
	{
		const int32_t fullWidth = widget->GetSize().x - 20;

		// Default footstep sound (used when the surface material doesn't specify
		// its own). Per-surface sounds are assigned on each Material in the
		// material editor; this is the fallback.
		auto* soundSearch = new AssetSearch(widget, widget->GetNextPos(), Point(fullWidth, 84),
			L"Default Footstep Sound", { ResourceType::Audio },
			[this](AssetSearch*, const AssetSearchResult& result)
			{
				const fs::path& chosen = !result.assetPath.empty() ? result.assetPath : result.absolutePath;
				_footstepSoundPath = chosen.string();
				_footstepSoundCache.clear(); // drop cached masters so the new sound loads
			});
		if (!_footstepSoundPath.empty())
			soundSearch->SetValue(std::wstring(_footstepSoundPath.begin(), _footstepSoundPath.end()));

		new DragFloat(widget, widget->GetNextPos(), Point(fullWidth, 18), L"Stride Length (m)", &_strideLength, 0.3f, 8.0f, 0.05f);
		new DragFloat(widget, widget->GetNextPos(), Point(fullWidth, 18), L"Footstep Volume", &_footstepVolume, 0.0f, 1.0f, 0.01f);
		new DragFloat(widget, widget->GetNextPos(), Point(fullWidth, 18), L"Pitch Variation", &_footstepPitchVariation, 0.0f, 0.5f, 0.01f);
		return true;
	}

	bool FirstPersonCameraController::OnInputEvent(InputEvent event, InputData* data)
	{
		if (event == InputEvent::MouseMove && data->MouseMove.absolute == false)
		{
			Camera* camera = GetEntity()->GetComponent<Camera>();

			if (g_pEnv->_sceneManager->GetCurrentScene()->GetMainCamera() == camera)
			{
				if (data->MouseMove.x != 0.0f)
				{
					camera->SetYaw(camera->GetYaw() - data->MouseMove.x);
				}
				if (data->MouseMove.y != 0.0f)
				{
					camera->SetPitch(camera->GetPitch() - data->MouseMove.y);
				}
			}
		}

		return false;
	}
}
