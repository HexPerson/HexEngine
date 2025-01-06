
#include "AnimatedMesh.hpp"
#include "../Environment/IEnvironment.hpp"
#include "../Environment/TimeManager.hpp"
#include "../Graphics/IGraphicsDevice.hpp"
#include "../Environment/LogFile.hpp"

namespace HexEngine
{
	AnimatedMesh::AnimatedMesh(std::shared_ptr<Model>& model, const std::string& name) :
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

	void AnimatedMesh::UpdateConstantBuffer(const math::Matrix& localTM, Material* material, int32_t instanceId)
	{
		Mesh::UpdateConstantBuffer(localTM, material, instanceId);

		// Write the per-object constant buffer
		auto perAnimBuffer = g_pEnv->_graphicsDevice->GetEngineConstantBuffer(EngineConstantBuffer::PerAnimationBuffer);


		if (!perAnimBuffer)
		{
			LOG_WARN("Invalid per-object constant buffer");
			return;
		}

		if (_animData && _animData->_animations.size() > 0 && _animData->_animIndex != -1)
		{
			Animation& anim = _animData->_animations.at(min(_animData->_animations.size() - 1, _animData->_animIndex));

			std::vector<math::Matrix> transforms;
			UpdateBoneTransform(&anim, g_pEnv->_timeManager->_currentTime, transforms);

			if (_animData->_nextAnimIndex != -1)
			{
				Animation& anim2 = _animData->_animations.at(min(_animData->_animations.size() - 1, _animData->_nextAnimIndex));

				std::vector<math::Matrix> blendTransforms;
				UpdateBoneTransform(&anim2, g_pEnv->_timeManager->_currentTime, blendTransforms);

				for (uint32_t i = 0; i < blendTransforms.size(); ++i)
				{
					transforms[i] = math::Matrix::Lerp(transforms[i], blendTransforms[i], _animData->_blendFactor);
				}

				_animData->_blendFactor += g_pEnv->_timeManager->_frameTime * 0.3f;

				if (_animData->_blendFactor >= 1.0f)
				{
					_animData->_animIndex = _animData->_nextAnimIndex;
					_animData->_nextAnimIndex = -1;
					_animData->_blendFactor = 0.0f;
				}
			}

			memcpy(_animationBuffer->_boneTransforms, (uint8_t*)transforms.data(), transforms.size() * sizeof(math::Matrix));
		}

		perAnimBuffer->Write(_animationBuffer, sizeof(PerAnimationBuffer));

		g_pEnv->_graphicsDevice->SetConstantBufferVS(3, perAnimBuffer);

	}

	void AnimatedMesh::UpdateBoneTransform(Animation* animation, float TimeInSeconds, std::vector<math::Matrix>& Transforms)
	{
		math::Matrix Identity;

		TimeInSeconds -= animation->time;

		//TimeInSeconds *= animation->speed;

		float TicksPerSecond = animation->ticksPerSecond != 0 ? animation->ticksPerSecond : 25.0f;

		float TimeInTicks = TimeInSeconds * TicksPerSecond;
		float AnimationTime = fmod(TimeInTicks, animation->duration);

		//if (abs(AnimationTime - animation->duration) < 0.5f)
		//	animation->speed *= -1.0f;

		ReadNodeHierarchy(animation->_rootNode, AnimationTime, Identity);

		Transforms.resize(_boneInfo.size());

		for (uint32_t i = 0; i < _boneInfo.size(); i++)
		{
			Transforms[i] = _boneInfo[i].FinalTransformation;// .Invert().Transpose();
			//Transforms[i] = Transforms[i].Invert().Transpose(); // this is to fix normals where animated meshes have been scaled non uniformly
		}
	}

	void AnimatedMesh::ReadNodeHierarchy(AnimChannel* animation, float AnimationTime, math::Matrix& ParentTransform)
	{
		auto transform = animation->nodeTransform;

		// Interpolate scaling and generate scaling transformation matrix
		math::Vector3 Scaling;
		CalcInterpolatedScaling(Scaling, AnimationTime, animation);
		math::Matrix ScalingM = math::Matrix::CreateScale(Scaling.x, Scaling.y, Scaling.z);

		// Interpolate rotation and generate rotation transformation matrix
		math::Quaternion RotationQ;
		CalcInterpolatedRotation(RotationQ, AnimationTime, animation);
		math::Matrix RotationM = math::Matrix::CreateFromQuaternion(RotationQ);

		// Interpolate translation and generate translation transformation matrix
		math::Vector3 Translation;
		CalcInterpolatedPosition(Translation, AnimationTime, animation);
		math::Matrix TranslationM = math::Matrix::CreateTranslation(Translation);

		// Combine the above transformations
		transform = ScalingM * RotationM * TranslationM;
		transform = transform.Transpose();

		math::Matrix GlobalTransformation = ParentTransform * transform;

		if (_boneMap.find(animation->nodeName) != _boneMap.end())
		{
			uint32_t BoneIndex = _boneMap[animation->nodeName];

			auto& boneInfo = _boneInfo[BoneIndex];

			boneInfo.FinalTransformation = _animData->_globalInverseTransform * GlobalTransformation * _boneInfo[BoneIndex].BoneOffset;

			auto transposeGlobalTransform = GlobalTransformation.Transpose();

			boneInfo.Position = transposeGlobalTransform.Translation();
			boneInfo.Rotation = math::Quaternion::CreateFromRotationMatrix(transposeGlobalTransform);
		}

		for (uint32_t i = 0; i < animation->children.size(); i++)
		{
			ReadNodeHierarchy(animation->children.at(i), AnimationTime, GlobalTransformation);
		}
	}

	const AnimChannel* AnimatedMesh::FindNodeAnim(const AnimChannel* pAnimation, const std::string& NodeName)
	{
		/*for (auto it = pAnimation->nodeToAnimMap.begin(); it != pAnimation->nodeToAnimMap.end(); it++)
		{
			if (it->first == NodeName)
				return it->second;
		}*/

		if (pAnimation->nodeName == NodeName)
			return pAnimation;

		for (auto& child : pAnimation->children)
		{
			auto res = FindNodeAnim(child, NodeName);

			if (res != nullptr)
				return res;
		}

		return nullptr;
	}

	void AnimatedMesh::CalcInterpolatedScaling(math::Vector3& Out, float AnimationTime, const AnimChannel* pNodeAnim)
	{
		if (pNodeAnim->scaleKeys.size() == 1) {
			Out = pNodeAnim->scaleKeys[0].second;
			return;
		}

		uint32_t ScalingIndex = FindScaling(AnimationTime, pNodeAnim);
		uint32_t NextScalingIndex = (ScalingIndex + 1);
		assert(NextScalingIndex < pNodeAnim->scaleKeys.size());
		float t1 = (float)pNodeAnim->scaleKeys[ScalingIndex].first - (float)pNodeAnim->scaleKeys[0].first;
		float t2 = (float)pNodeAnim->scaleKeys[NextScalingIndex].first - (float)pNodeAnim->scaleKeys[0].first;
		float DeltaTime = t2 - t1;
		float Factor = std::clamp((AnimationTime - (float)t1) / DeltaTime, 0.0f, 1.0f);
		assert(Factor >= 0.0f && Factor <= 1.0f);
		const math::Vector3& Start = pNodeAnim->scaleKeys[ScalingIndex].second;
		const math::Vector3& End = pNodeAnim->scaleKeys[NextScalingIndex].second;
		math::Vector3 Delta = End - Start;
		Out = Start + Factor * Delta;
	}

	void AnimatedMesh::CalcInterpolatedPosition(math::Vector3& Out, float AnimationTime, const AnimChannel* pNodeAnim)
	{
		if (pNodeAnim->positionKeys.size() == 1) {
			Out = pNodeAnim->positionKeys[0].second;
			return;
		}

		uint32_t PositionIndex = FindPosition(AnimationTime, pNodeAnim);
		uint32_t NextPositionIndex = (PositionIndex + 1);
		assert(NextPositionIndex < pNodeAnim->positionKeys.size());
		float t1 = (float)pNodeAnim->positionKeys[PositionIndex].first - (float)pNodeAnim->positionKeys[0].first;
		float t2 = (float)pNodeAnim->positionKeys[NextPositionIndex].first - (float)pNodeAnim->positionKeys[0].first;
		float DeltaTime = t2 - t1;
		float Factor = std::clamp((AnimationTime - (float)t1) / DeltaTime, 0.0f, 1.0f);
		assert(Factor >= 0.0f && Factor <= 1.0f);
		const math::Vector3& Start = pNodeAnim->positionKeys[PositionIndex].second;
		const math::Vector3& End = pNodeAnim->positionKeys[NextPositionIndex].second;
		math::Vector3 Delta = End - Start;
		Out = Start + Factor * Delta;
	}


	void AnimatedMesh::CalcInterpolatedRotation(math::Quaternion& Out, float AnimationTime, const AnimChannel* pNodeAnim)
	{
		// we need at least two values to interpolate...
		if (pNodeAnim->rotationKeys.size() == 1) {
			Out = pNodeAnim->rotationKeys[0].second;
			return;
		}

		uint32_t RotationIndex = FindRotation(AnimationTime, pNodeAnim);
		uint32_t NextRotationIndex = (RotationIndex + 1);
		assert(NextRotationIndex < pNodeAnim->rotationKeys.size());
		float t1 = (float)pNodeAnim->rotationKeys[RotationIndex].first - (float)pNodeAnim->rotationKeys[0].first;
		float t2 = (float)pNodeAnim->rotationKeys[NextRotationIndex].first - (float)pNodeAnim->rotationKeys[0].first;
		float DeltaTime = t2 - t1;
		float Factor = std::clamp((AnimationTime - (float)t1) / DeltaTime, 0.0f, 1.0f);
		assert(Factor >= 0.0f && Factor <= 1.0f);
		const math::Quaternion& StartRotationQ = pNodeAnim->rotationKeys[RotationIndex].second;
		const math::Quaternion& EndRotationQ = pNodeAnim->rotationKeys[NextRotationIndex].second;

		math::Quaternion::Slerp(StartRotationQ, EndRotationQ, Factor, Out);
		Out.Normalize();
	}

	uint32_t AnimatedMesh::FindScaling(float AnimationTime, const AnimChannel* pNodeAnim)
	{
		assert(pNodeAnim->scaleKeys.size() > 0);

		for (uint32_t i = 0; i < pNodeAnim->scaleKeys.size() - 1; i++) {
			float t = (float)pNodeAnim->scaleKeys[i + 1].first - (float)pNodeAnim->scaleKeys[0].first;
			if (AnimationTime <= t) {
				return i;
			}
		}

		return (uint32_t)pNodeAnim->scaleKeys.size() - 2;
	}

	uint32_t AnimatedMesh::FindPosition(float AnimationTime, const AnimChannel* pNodeAnim)
	{
		for (uint32_t i = 0; i < pNodeAnim->positionKeys.size() - 1; i++) {
			float t = (float)pNodeAnim->positionKeys[i + 1].first - (float)pNodeAnim->positionKeys[0].first;
			if (AnimationTime <= t) {
				return i;
			}
		}

		return (uint32_t)pNodeAnim->positionKeys.size() - 2;
	}

	uint32_t AnimatedMesh::FindRotation(float AnimationTime, const AnimChannel* pNodeAnim)
	{
		assert(pNodeAnim->rotationKeys.size() > 0);

		for (uint32_t i = 0; i < pNodeAnim->rotationKeys.size() - 1; i++) {
			float t = (float)pNodeAnim->rotationKeys[i + 1].first - (float)pNodeAnim->rotationKeys[0].first;
			if (AnimationTime <= t) {
				return i;
			}
		}

		return (uint32_t)pNodeAnim->rotationKeys.size() - 2;
	}

	void AnimatedMesh::StopAnimating()
	{
		_animData->_animIndex = -1;
	}

	void AnimatedMesh::SetAnimationIndex(uint32_t idx)
	{
		if (idx < 0 || idx >= _animData->_animations.size())
			return;

		_animData->_animIndex = idx;

		_animData->_animations.at(idx).time = g_pEnv->_timeManager->_currentTime;
	}

	void AnimatedMesh::BlendToAnimationIndex(uint32_t idx)
	{
		// no point blending into the animation we are already running
		if ((_animData->_animIndex == idx /*&& _animData->_blendFactor == 0.0f*/) || _animData->_nextAnimIndex == idx)
			return;

		_animData->_blendFactor = 0.0f;
		_animData->_nextAnimIndex = idx;

		_animData->_animations.at(idx).time = g_pEnv->_timeManager->_currentTime;
	}

	const std::shared_ptr<AnimationData>& AnimatedMesh::GetAnimationData() const
	{
		return _animData;
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
}