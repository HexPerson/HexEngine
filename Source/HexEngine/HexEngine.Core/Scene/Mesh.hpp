
#pragma once

#include "../Required.hpp"
#include "../Graphics/IVertexBuffer.hpp"
#include "../Graphics/IIndexBuffer.hpp"
#include "../Graphics/IShader.hpp"
#include "../Graphics/IInputLayout.hpp"
#include "../Graphics/ITexture2D.hpp"
#include "MeshInstance.hpp"
//#include "../Terrain/TerrainGenerator.hpp"

namespace HexEngine
{
	struct MeshVertex
	{
		MeshVertex()
		{

		}

		static MeshVertex Create(const math::Vector3& pos, const math::Vector3& normal, const math::Vector3& tangent, float u, float v)
		{
			MeshVertex vert;

			vert._position = math::Vector4(pos.x, pos.y, pos.z, 1.0f);
			vert._normal = normal;
			vert._tangent = tangent;
			vert._texcoord.x = u;
			vert._texcoord.y = v;

			return vert;
		}		

		math::Vector4 _position;
		math::Vector3 _normal;
		math::Vector3 _tangent;		
		math::Vector3 _bitangent;
		math::Vector2 _texcoord;
		//math::Vector4 _boneIds;
		//math::Vector4 _boneWeights;
	};

	using MeshIndexFormat = uint32_t;

	struct AnimatedMeshVertex : MeshVertex
	{
		void AddBoneData(uint32_t BoneID, float Weight)
		{
			for (uint32_t i = 0; i < 4; i++)
			{
				float* weights = &_boneWeights.x;
				float* ids = &_boneIds.x;

				if (weights[i] == 0.0)
				{
					ids[i] = (float)BoneID;
					weights[i] = Weight;
					return;
				}
			}

			// should never get here - more bones than we have space for
			//    assert(0);
		}

		math::Vector4 _boneIds;
		math::Vector4 _boneWeights;
	};

	struct BoneInfo
	{
		math::Matrix BoneOffset;
		math::Matrix FinalTransformation;
		math::Vector3 Position;
		math::Quaternion Rotation;

		BoneInfo()
		{
			BoneOffset = math::Matrix::Identity;
			FinalTransformation = math::Matrix::Identity;
		}
	};

	struct AnimChannel
	{
		std::string nodeName;
		std::vector<std::pair<float,math::Vector3>> positionKeys;
		std::vector<std::pair<float,math::Quaternion>> rotationKeys;
		std::vector<std::pair<float,math::Vector3>> scaleKeys;
		math::Matrix nodeTransform;

		std::vector<AnimChannel*> children;
	};

	struct Animation
	{
		std::string name;
		float ticksPerSecond;
		float duration;
		float speed = 0.5f;
		float time = 0.0f;

		AnimChannel* _rootNode = nullptr;
		std::vector<AnimChannel> channels;
		//std::unordered_map<std::string, AnimChannel*> nodeToAnimMap;
	};

	struct AnimationData
	{
		std::vector<Animation> _animations;		
		math::Matrix _globalInverseTransform;
		uint32_t _animIndex = 1;
		uint32_t _nextAnimIndex = -1;
		float _blendFactor = 0.0f;
	};

	const int OBJECT_FLAGS_HAS_BUMP					= (1 << 0);
	const int OBJECT_FLAGS_HAS_ROUGHNESS			= (1 << 1);
	const int OBJECT_FLAGS_HAS_METALLIC				= (1 << 2);
	const int OBJECT_FLAGS_HAS_HEIGHT				= (1 << 3);
	const int OBJECT_FLAGS_HAS_EMISSION				= (1 << 4);
	const int OBJECT_FLAGS_HAS_OPACITY				= (1 << 5);
	const int OBJECT_FLAGS_HAS_AMBIENT_OCCLUSION	= (1 << 6);
	const int OBJECT_FLAGS_HAS_ANIMATION			= (1 << 7);

	class MeshRenderer;
	class Model;
	class FileSystem;
	class Material;
	
	class Mesh : public IResource
	{
	public:
		friend class AssimpModelImporter;
		friend class MeshRenderer;
		friend class IconService;

		Mesh();
		Mesh(const std::shared_ptr<Model>& model, const std::string& name);
		Mesh(Mesh* other);

		virtual ~Mesh();

		static std::shared_ptr<Mesh> Create(const fs::path& path);

		//static Mesh* CreatePlane(uint32_t resolution, float uvScale = 1.0f);

		// Materials
		std::shared_ptr<Material>	GetMaterial() const;
		void						SetMaterial(std::shared_ptr<Material> material);

		bool CreateBuffers();
		//bool CreateBuffers(bool dynamic, uint32_t stride, uint32_t count, void* data);

		virtual void Destroy() override;

		void SetBuffers(const math::Matrix& worldMatrix);

		virtual void UpdateConstantBuffer(const math::Matrix& localTM, Material* material, int32_t instanceId);
		

		void Clear();
		void AddVertex(const MeshVertex& vertex);
		void AddIndex(const MeshIndexFormat index);
		void AddVertices(const std::vector<MeshVertex>& vertices);
		void AddIndices(const std::vector<MeshIndexFormat>& indices);
		void SetNumFaces(uint32_t numFaces);
		
		const std::vector<MeshIndexFormat>& GetIndices() const;
		const std::vector<MeshVertex>& GetVertices() const;
		uint32_t GetNumFaces() const;
		uint32_t GetNumIndices() const;
		uint32_t GetNumVertices() const;

		const dx::BoundingBox& GetAABB() const;
		const dx::BoundingOrientedBox& GetOBB() const;		

		void SetAABB(const dx::BoundingBox& aabb);
		void SetOBB(const dx::BoundingOrientedBox& obb);

		MeshInstance* GetInstance();

		//void SetPaths(const fs::path& path, FileSystem* fileSystem);

		const std::string& GetName();
		const std::string& GetFullName();

		Mesh* Clone();
		MeshInstance* CreateInstance();

		int32_t		GetLodLevel() const;		
		void		SetLodLevel(int32_t level);
		int32_t		GetMaxLodLevel() const;
		void		SetMaxLodLevel(int32_t level);

		virtual bool HasAnimations() const { return false; }

	public:
		MeshInstance* _instance = nullptr;
		//TerrainGenerationParams _terrainParams;
		math::Matrix _modelTransform;

	protected:
		IVertexBuffer* _vertexBuffer = nullptr;
		IIndexBuffer* _indexBuffer = nullptr;
		std::vector<MeshIndexFormat> _indices;

	private:
		std::shared_ptr<Model> _model;
		//std::string _fileName;
		std::string _name;

		std::string _cachedName;
		std::string _cachedFullName;
		std::string _materialName;
		
		std::vector<MeshVertex> _vertices;
		uint32_t _faceCount = 0;		

		dx::BoundingBox _aabb;
		dx::BoundingOrientedBox _obb;		

		int32_t _lodLevel = -1;
		int32_t _maxLodLevel = -1;

		struct PerObjectBuffer* _objectBuffer = nullptr;

	private:
		// Materials
		std::shared_ptr<Material> _material = nullptr;
	};
}
