
#include "NavigationComponent.hpp"
#include "Transform.hpp"
#include "../Entity.hpp"
#include "../../Math/FloatMath.hpp"
#include "../../Math/easing.h"
#include "../../GUI/UIManager.hpp"
#include "../../Scene/SceneManager.hpp"

namespace HexEngine
{
	NavigationComponent::NavigationComponent(Entity* entity) :
		UpdateComponent(entity)
	{

		//g_pEnv->_debugGui->AddCallback(this);
	}

	NavigationComponent::NavigationComponent(Entity* entity, NavigationComponent* copy) :
		UpdateComponent(entity, copy)
	{
		//g_pEnv->_debugGui->AddCallback(this);
	}

	NavigationComponent::~NavigationComponent()
	{
		//g_pEnv->_debugGui->RemoveCallback(this);
	}

	void NavigationComponent::FindPath(const math::Vector3& from, const math::Vector3& to, float stepSize)
	{
		_result.path.clear();
		_pathIndex = 0;

		INavMeshProvider::PathParams path;
		path.from = from;
		path.to = to;
		path.searchDistance = math::Vector3(500.0f);
		path.stepSize = stepSize;

		_targetPosition = to;

		g_pEnv->_navMeshProvider->FindPath(path, _result);
	}

	void NavigationComponent::Update(float dt)
	{
		UpdateComponent::Update(dt);

		if (_result.path.size() > 0)
		{
			INavMeshProvider::MoveResult result = g_pEnv->_navMeshProvider->Move(_result);

			if (result == INavMeshProvider::MoveResult::Moving)
			{
				auto transform = GetEntity()->GetComponent<Transform>();

				auto currentPos = transform->GetPosition();

				auto delta = _result.path[_pathIndex] - currentPos;//_result.path[_pathIndex + 0];

				float length = delta.Length();
				delta.Normalize();

				if (length <= 5.0f)
				{
					++_pathIndex;

					if (_pathIndex >= _result.path.size())
					{
						//if (_reachedDestinationFn)
						//	_reachedDestinationFn(this);

						NavigationTargetReachedMessage message;
						message.targetPosition = _targetPosition;
						message.finalPosition = currentPos;

						GetEntity()->OnMessage(&message, this);

						_pathIndex = 0;
						_result.path.clear();

						//g_pEnv->_navMeshProvider->FindPath(path, _result);
						return;
					}
				}
	

				currentPos += delta * dt * 200.0f;

				transform->SetPosition(currentPos);

				float yaw = atan2(delta.x, delta.z);

				auto targetRotation = math::Quaternion::CreateFromYawPitchRoll(yaw, 0.0f, 0.0f);

				if (yaw != _targetYaw /*&& _hasNewRotation == false*/)
				{
					_rotationTime = 0.0f;
					_targetYaw = yaw;
					_targetRotation = targetRotation;
					_hasNewRotation = true;
				}

				_rotationTime += dt * _rotationSpeed;
				_rotationTime = std::clamp(_rotationTime, 0.0f, 1.0f);

				auto easingFunction = getEasingFunction(EaseOutCubic);

				if(_hasNewRotation)
					transform->SetRotation(math::Quaternion::Slerp(transform->GetRotation(), _targetRotation, easingFunction(_rotationTime)));

				if (_rotationTime >= 1.0f)
				{
					_hasNewRotation = false;
				}

				//transform->SetYaw(yaw);
				

				//_result.currentPos = currentPos;
			}
			/*else if (result == INavMeshProvider::MoveResult::ReachedDestination)
			{
				INavMeshProvider::PathParams path;
				path.from = _result.currentPos;
				path.to = math::Vector3(GetRandomFloat(-5000.0f, 5000.0f), 0.0f, GetRandomFloat(-5000.0f, 5000.0f));
				path.searchDistance = math::Vector3(500.0f);

				g_pEnv->_navMeshProvider->FindPath(path, _result);
			}
			else if (result == INavMeshProvider::MoveResult::Failed)
			{
				_hasPath = false;
			}*/
		}
	}

	void NavigationComponent::OnDebugGUI()
	{
		auto renderer = g_pEnv->_uiManager->GetRenderer();

		int32_t x, y;

		if (_result.path.size() > 0)
		{
			for (auto& r : _result.path)
			{
				if (g_pEnv->_inputSystem->GetWorldToScreenPosition(
					g_pEnv->_sceneManager->GetCurrentScene()->GetMainCamera(),
					r,
					x, y))
				{
					renderer->FillQuad(x - 1, y - 1, 2, 2, math::Color(HEX_RGB_TO_FLOAT3(255, 242, 0), 1.0f));
				}
			}
		}

		if (g_pEnv->_inputSystem->GetWorldToScreenPosition(
			g_pEnv->_sceneManager->GetCurrentScene()->GetMainCamera(),
			GetEntity()->GetPosition(),
			x, y))
		{
			renderer->PrintText(
				renderer->_style.font.get(),
				(uint8_t)Style::FontSize::Regular,
				x, y,
				math::Color(0xFFFFFFFF),
				FontAlign::CentreLR,
				std::format(L"Index: {}/{}", _pathIndex, _result.path.size()));

			renderer->PrintText(
				renderer->_style.font.get(),
				(uint8_t)Style::FontSize::Regular,
				x, y+20,
				math::Color(0xFFFFFFFF),
				FontAlign::CentreLR,
				std::format(L"Rotation: {:2f}", _rotationTime));
		}
	}
}