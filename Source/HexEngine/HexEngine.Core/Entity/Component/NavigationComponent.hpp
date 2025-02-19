
#pragma once

#include "UpdateComponent.hpp"
#include "../../Scene/INavMeshProvider.hpp"
#include "../../GUI/DebugGUI.hpp"

namespace HexEngine
{
	

	class NavigationComponent : public UpdateComponent, public IDebugGUICallback
	{
	public:
		CREATE_COMPONENT_ID(NavigationComponent);
		DEFINE_COMPONENT_CTOR(NavigationComponent);

		//using ReachedDestinationFn = void (BaseComponent*);

		virtual ~NavigationComponent();

		void FindPath(const math::Vector3& from, const math::Vector3& to, float stepSize);

		virtual void Update(float dt) override;

		virtual void OnDebugGUI() override;

		void SetRotationSpeed(float speed) { _rotationSpeed = speed; }

		//void SetReachedDestinationFn(std::function<ReachedDestinationFn> fn) { _reachedDestinationFn = fn; }

	private:
		math::Vector3 _targetPosition;
		INavMeshProvider::PathResult _result;
		uint32_t _pathIndex = 0;
		//math::Quaternion _currentRotation;
		math::Quaternion _targetRotation;
		float _rotationTime = 0.0f;
		float _rotationSpeed = 2.0f;
		bool _hasNewRotation = false;
		float _targetYaw = 0.0f;

		//std::function<ReachedDestinationFn> _reachedDestinationFn;
	};
}
