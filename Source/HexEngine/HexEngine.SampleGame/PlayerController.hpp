

#pragma once

#include "../HexEngine.Core/Entity/Camera.hpp"
#include "../HexEngine.Core/Entity/BaseChunkEntity.hpp"
#include "../HexEngine.Core/Entity/Component/RigidBody.hpp"
#include "../HexEngine.Core/Physics/IController.hpp"
#include "PlayerMovement.hpp"

namespace CityBuilder
{
	struct PlayerBodyData
	{
		float height = 64.0f;
		float mass = 90.0f;
		float radius = 16.0f;
	};

	class PlayerController : public HexEngine::Entity
	{
	public:
		PlayerController();

		virtual void Create() override;

		virtual void Update(float frameTime) override;

		virtual void FixedUpdate(float frameTime) override;

		virtual void OnTransformChanged(bool scaleChanged, bool rotationChanged, bool translationChanged) override;

	private:
		HexEngine::Camera* _mainCamera = nullptr;
		//HexEngine::RigidBody* _rigidBody = nullptr;
		PlayerBodyData _bodyData;
		HexEngine::IRigidBody* _controller = nullptr;

		PlayerMovement _movement;
		float _pickupDistance = 80.0f;
	};
}
