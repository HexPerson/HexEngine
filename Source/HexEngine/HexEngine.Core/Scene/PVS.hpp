
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
	class Camera;

	struct PVSParams
	{
		PVSParams() :
			shapeType(ShapeType::Frustum),
			lodPartition(0.0f),
			forceMaxLod(false),
			isShadow(false),
			camera(nullptr)
		{}

		enum class ShapeType
		{
			Sphere,
			Frustum,
			Frustum2
		};

		float lodPartition;
		bool forceMaxLod;

		union Shape
		{
			constexpr Shape() {}

			dx::BoundingSphere sphere;

			struct
			{
				dx::BoundingFrustum sm;
				dx::BoundingFrustum lg;
			} frustum;

		} shape;

		math::Matrix shadowViewMatrix;
		Camera* camera;
		ShapeType shapeType;
		bool isShadow;
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

		uint32_t GetTotalNumberOfEnts() const { return _totalEnts; }
		uint32_t GetTotalSkeletalAnimators() const { return _totalSkeletalAnimators; }

	private:
		MeshInstanceMap _pvs;
		PVSParams _optimisedParams;
		bool _needsOptimisationRebuild = true;
		bool _hasBuildOptimisation = false;
		bool _forceRebuild = true;
		std::recursive_mutex _lock;

		uint32_t _totalEnts = 0;
		uint32_t _totalSkeletalAnimators = 0;
	};
}
