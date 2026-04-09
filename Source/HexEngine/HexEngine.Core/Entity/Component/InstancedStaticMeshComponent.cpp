
#include "InstancedStaticMeshComponent.hpp"

namespace HexEngine
{
	InstancedStaticMeshComponent::InstancedStaticMeshComponent(Entity* parent) :
		StaticMeshComponent(parent)
	{
	}

	InstancedStaticMeshComponent::InstancedStaticMeshComponent(Entity* parent, InstancedStaticMeshComponent* clone) :
		StaticMeshComponent(parent, clone)
	{
	}

	InstancedStaticMeshComponent::~InstancedStaticMeshComponent()
	{
	}

	uint32_t InstancedStaticMeshComponent::AddInstance(const math::Matrix& mat)
	{
		_instances.push_back(mat);
		return (uint32_t)_instances.size() - 1u;
	}

	void InstancedStaticMeshComponent::RemoveInstance(uint32_t id)
	{
		if (id < 0 || id >= _instances.size())
			return;

		_instances.erase(_instances.begin() + id);
	}

	const std::vector<math::Matrix>& InstancedStaticMeshComponent::GetInstances() const
	{
		return _instances;
	}
}