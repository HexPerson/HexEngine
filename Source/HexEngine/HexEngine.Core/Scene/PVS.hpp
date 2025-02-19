
#pragma once

#include "../Required.hpp"

namespace HexEngine
{
	class MeshInstance;
	class Mesh;
	class Entity;
	class Scene;
	class Material;
	class BaseComponent;

	struct PVSParams
	{
		PVSParams() :
			shapeType(ShapeType::Frustum),
			lodPartition(0.0f),
			forceMaxLod(false)
		{}

		enum class ShapeType
		{
			Sphere,
			Frustum
		};

		float lodPartition;
		bool forceMaxLod;

		union Shape
		{
			constexpr Shape() {}

			dx::BoundingSphere sphere;
			dx::BoundingFrustum frustum;
		} shape;

		math::Matrix shadowViewMatrix;

		ShapeType shapeType;
	};

	class PVS
	{
	public:
		//struct MeshMapComp
		//{
		//	//template<typename T>
		//	bool operator()(Material* l, Material* r) const
		//	{
		//		if(l->)
		//	}
		//};


		using MeshEntityPair = std::tuple<std::shared_ptr<Mesh>, Entity*, BaseComponent*>;
		using MeshEntityVector = std::vector<MeshEntityPair>;
		//using MaterialEntityVectorPair = std::pair<, MeshEntityVector>;
		using MeshInstanceMap = std::map<std::shared_ptr<Material>, MeshEntityVector>;

		void ClearPVS(); 
		void ForceRebuild();
		bool NeedsRebuild() const;
		const MeshInstanceMap& GetRenderables() const;

		void CalculateVisibility(Scene* scene, const PVSParams& params);

		bool IsEntityVisible(Entity* entity, const PVSParams& params);
		bool IsShapeVisible(const dx::BoundingBox& bbox, const PVSParams& params);
		bool IsShapeVisible(const dx::BoundingSphere& bsphere, const PVSParams& params);

		void AddEntity(Entity* entity);
		void RemoveEntity(Entity* entity);

		const PVSParams& GetOptimisedParams() const;

	private:
		MeshInstanceMap _pvs;
		PVSParams _optimisedParams;
		bool _needsOptimisationRebuild = true;
		bool _hasBuildOptimisation = false;
		bool _forceRebuild = true;
		std::recursive_mutex _lock;
	};
}
