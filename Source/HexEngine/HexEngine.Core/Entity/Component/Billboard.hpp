
#pragma once

#include "UpdateComponent.hpp"
#include "../../Graphics/ITexture2D.hpp"

namespace HexEngine
{
	class Camera;
	class Mesh;

	class HEX_API Billboard : public UpdateComponent
	{
	public:
		CREATE_COMPONENT_ID(Billboard);

		Billboard(Entity* entity);

		Billboard(Entity* entity, Billboard* copy);

		virtual void Update(float frameTime) override;

		void SetTexture(const std::shared_ptr<ITexture2D>& texture);

	private:
		ITexture2D* _texture;
		math::Matrix _billboardMatrix;
		std::shared_ptr<Mesh> _mesh;
	};
}
