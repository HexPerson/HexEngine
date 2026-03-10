
#include "AnimatedMesh.hpp"
#include "../Environment/IEnvironment.hpp"
#include "../Environment/TimeManager.hpp"
#include "../Graphics/IGraphicsDevice.hpp"
#include "../Environment/LogFile.hpp"
#include "../Entity/Entity.hpp"
#include "../Entity/Component/SkeletalAnimationComponent.hpp"

namespace HexEngine
{
	AnimatedMesh::AnimatedMesh(const std::shared_ptr<Model>& model, const std::string& name) :
		Mesh(model, name)
	{
		_animationBuffer = new PerAnimationBuffer;
	}

	AnimatedMesh::AnimatedMesh(AnimatedMesh* other) :
		Mesh(other)
	{
		_boneInfo = other->_boneInfo;
		_boneMap = other->_boneMap;
		_rootTransformation = other->_rootTransformation;

		//_terrainParams = other->_terrainParams;

		if (other->_animData)
			_animData = other->_animData;

		_vertices = other->_vertices;
		_transformedVertices = other->_transformedVertices;

		_animationBuffer = new PerAnimationBuffer;
	}

	AnimatedMesh::~AnimatedMesh()
	{
		//Destroy();

		SAFE_DELETE(_animationBuffer);
	}

	void AnimatedMesh::UpdateConstantBuffer(Entity* entity, const math::Matrix& localTM, Material* material, int32_t instanceId)
	{
		Mesh::UpdateConstantBuffer(entity, localTM, material, instanceId);

		if (auto skeletalMeshComp = entity->GetComponent<SkeletalAnimationComponent>(); skeletalMeshComp != nullptr)
		{
			// Write the per-object constant buffer
			auto perAnimBuffer = g_pEnv->_graphicsDevice->GetEngineConstantBuffer(EngineConstantBuffer::PerAnimationBuffer);

			if (!perAnimBuffer)
			{
				LOG_WARN("Invalid per-object constant buffer");
				return;
			}
			const auto& transforms = skeletalMeshComp->GetBoneTransformArray();

			memcpy(_animationBuffer->_boneTransforms, (uint8_t*)transforms.data(), transforms.size() * sizeof(math::Matrix));


			perAnimBuffer->Write(_animationBuffer, sizeof(PerAnimationBuffer));

			g_pEnv->_graphicsDevice->SetConstantBufferVS(3, perAnimBuffer);
		}
	}

	const std::vector<AnimatedMeshVertex>& AnimatedMesh::GetVertices() const
	{
		return _vertices;
	}

	std::shared_ptr<AnimationData> AnimatedMesh::GetAnimationData() const
	{
		return _animData;
	}

	void AnimatedMesh::SetBoneMap(uint32_t numBones, const BoneNameMap& boneMap, const BoneInfoArray& boneInfo)
	{
		if (boneMap.size() != numBones)
		{
			LOG_CRIT("Number of bones and bone map size do not match, this is an invalid bone map");
			return;
		}

		if (boneMap.size() >= MAX_BONES || numBones >= MAX_BONES)
		{
			LOG_CRIT("Number of bones being assigned to AnimatedMesh exceeds the max bone limit");
			return;
		}

		_boneMap = boneMap;
		_numBones = numBones;
		_boneInfo = boneInfo;
	}

	std::shared_ptr<AnimationData> AnimatedMesh::CreateAnimationData()
	{
		return std::make_shared<AnimationData>();
	}

	BoneInfo* AnimatedMesh::GetBoneInfoByName(const std::string& name)
	{
		for (auto& bone : _boneMap)
		{
			if (bone.first == name)
			{
				return &_boneInfo[bone.second];
			}
		}
		return nullptr;
	}

	void AnimatedMesh::AddVertex(const AnimatedMeshVertex& vertex)
	{
		_vertices.push_back(vertex);
		_transformedVertices.push_back(vertex);

		Mesh::AddVertex(vertex);

		SimpleAnimatedMeshVertex simpleVertex;
		simpleVertex._position = vertex._position;
		simpleVertex._texcoord = vertex._texcoord;
		simpleVertex._boneIds = vertex._boneIds;
		simpleVertex._boneWeights = vertex._boneWeights;

		_simpleVertices.push_back(simpleVertex);
	}

	void AnimatedMesh::AddVertices(const std::vector<AnimatedMeshVertex>& vertex)
	{
		_vertices.insert(_vertices.end(), vertex.begin(), vertex.end());

		for (auto& vert : vertex)
		{
			SimpleAnimatedMeshVertex simpleVertex;
			simpleVertex._position = vert._position;
			simpleVertex._texcoord = vert._texcoord;
			simpleVertex._boneIds = vert._boneIds;
			simpleVertex._boneWeights = vert._boneWeights;

			_simpleVertices.push_back(simpleVertex);
		}
	}

	bool AnimatedMesh::CreateBuffers()
	{
		if (_vertexBuffer != nullptr || _indexBuffer != nullptr)
		{
			LOG_CRIT("Trying to create graphics buffers for a Mesh when it already has a buffer is not a valid operation. Please clear the Mesh buffers first");
			return false;
		}

		_vertexBuffer = g_pEnv->_graphicsDevice->CreateVertexBuffer(
			(int32_t)_vertices.size() * sizeof(AnimatedMeshVertex),
			sizeof(AnimatedMeshVertex),
			D3D11_USAGE_DEFAULT,
			0,
			_vertices.data());

		if (!_vertexBuffer)
		{
			return false;
		}

		if (_simpleVertices.size() > 0)
		{
			_simpleVertexBuffer = g_pEnv->_graphicsDevice->CreateVertexBuffer(
				(int32_t)_vertices.size() * sizeof(SimpleAnimatedMeshVertex),
				sizeof(SimpleAnimatedMeshVertex),
				D3D11_USAGE_DEFAULT,
				0,
				_simpleVertices.data());

			if (!_simpleVertexBuffer)
			{
				return false;
			}
		}

		_indexBuffer = g_pEnv->_graphicsDevice->CreateIndexBuffer(
			(int32_t)_indices.size() * sizeof(MeshIndexFormat),
			sizeof(MeshIndexFormat),
			D3D11_USAGE_DEFAULT,
			0,
			_indices.data());

		if (!_indexBuffer)
		{
			return false;
		}

		return true;
	}

	void AnimatedMesh::SetRootTransformation(const math::Matrix& rootTrans)
	{
		_rootTransformation = rootTrans;
	}

	const math::Matrix& AnimatedMesh::GetRootTransformation() const
	{
		return _rootTransformation;
	}
}