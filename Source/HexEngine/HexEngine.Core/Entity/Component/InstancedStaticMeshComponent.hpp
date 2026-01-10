
#pragma once

#include "BaseComponent.hpp"
#include "StaticMeshComponent.hpp"
#include "../../Scene/Mesh.hpp"
#include "../../Graphics/Material.hpp"
#include "../../Graphics/IGraphicsDevice.hpp"

namespace HexEngine
{
	class HEX_API InstancedStaticMeshComponent : public StaticMeshComponent
	{
	public:
		CREATE_COMPONENT_ID(InstancedStaticMeshComponent);

		InstancedStaticMeshComponent(Entity* entity);

		InstancedStaticMeshComponent(Entity* entity, InstancedStaticMeshComponent* clone);

		virtual ~InstancedStaticMeshComponent();

		//virtual void Destroy() override;

		uint32_t AddInstance(const math::Matrix& mat);
		void RemoveInstance(uint32_t id);
		const std::vector<math::Matrix>& GetInstances() const;

	private:
		std::vector<math::Matrix> _instances;
	};
}
