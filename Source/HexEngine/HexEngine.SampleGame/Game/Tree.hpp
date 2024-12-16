

#pragma once

#include "../../HexEngine.Core/Entity/Entity.hpp"
#include "Foliage.hpp"

namespace CityBuilder
{
	// {A0F58DDF-949D-4DC2-93CE-FE70FB05F5C3}
	DEFINE_HEX_GUID(TreeGUID,
		0xa0f58ddf, 0x949d, 0x4dc2, 0x93, 0xce, 0xfe, 0x70, 0xfb, 0x5, 0xf5, 0xc3);

	class Tree : public HexEngine::Entity
	{
	public:
		DEFINE_OBJECT_GUID(Tree);

		Tree();

		static Tree* Create(const math::Vector3& position);

		virtual void Destroy() override;

		virtual void Create() override;

		virtual void Update(float frameTime) override;

	private:
		void CreateFoliage(HexEngine::Mesh* mesh, const math::Vector3& position, float scale, float rotation);

	private: 
		HexEngine::Model* _treeModel = nullptr;
		std::vector<Foliage*> _foliage;

		float _scale = 0.0f; // initially the foliage should have a scale of 0
		float _targetScale = 1.0f;
	};
}
