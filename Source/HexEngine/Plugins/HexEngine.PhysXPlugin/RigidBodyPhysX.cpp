

#include "RigidBodyPhysX.hpp"
#include "PhysicsSystemPhysX.hpp"
#include "ColliderPhysX.hpp"
#include <HexEngine.Core/Environment/LogFile.hpp>
#include <HexEngine.Core/Entity/Entity.hpp>
#include <HexEngine.Core/Entity/Component/RigidBody.hpp>
#include <chrono>
#include <cmath>

namespace
{
	constexpr float kMinTriangleMeshScale = 0.001f;
	constexpr float kTriangleMeshWeldTolerance = 0.001f;

	float SanitizeTriangleMeshScaleAxis(float value)
	{
		if (!std::isfinite(value))
			return 1.0f;

		const float absValue = std::abs(value);
		return absValue < kMinTriangleMeshScale ? kMinTriangleMeshScale : absValue;
	}

	physx::PxMeshScale BuildSafeTriangleMeshScale(const math::Vector3& requestedScale)
	{
		return physx::PxMeshScale(physx::PxVec3(
			SanitizeTriangleMeshScaleAxis(requestedScale.x),
			SanitizeTriangleMeshScaleAxis(requestedScale.y),
			SanitizeTriangleMeshScaleAxis(requestedScale.z)));
	}
}

RigidBodyPhysX::RigidBodyPhysX(physx::PxRigidActor* body, HexEngine::RigidBody* bodyComponent, HexEngine::Entity* entity) :
	_body(body),
	_bodyComponent(bodyComponent),
	_entity(entity)
{
}

void RigidBodyPhysX::Create(HexEngine::Transform* transform)
{
	_transform = transform;
}

void RigidBodyPhysX::Destroy()
{
	if (_shape)
		_body->detachShape(*_shape);

	SAFE_DELETE(_collider);
	PX_RELEASE(_body);
	SAFE_DELETE(_geometry);
}

void RigidBodyPhysX::SetBodyType(BodyType type)
{
	// TODO
	if (type != _bodyType && _bodyType != BodyType::None)
	{
		if (_body)
		{
			g_pPhysx->GetScene()->lockWrite();

			if (_shape)
				_shape->acquireReference();
			//_body->detachShape(*_shape);

			PX_RELEASE(_body);

			physx::PxRigidActor* actor = nullptr;
			const auto worldTM = _transform->GetEntity()->GetWorldTM();
			const auto worldPos = worldTM.Translation();
			math::Quaternion worldRotation = _transform->GetRotation();
			for (auto* parent = _transform->GetEntity()->GetParent(); parent != nullptr; parent = parent->GetParent())
			{
				worldRotation = worldRotation * parent->GetRotation();
				worldRotation.Normalize();
			}
			physx::PxTransform localTm(
				physx::PxVec3(worldPos.x, worldPos.y, worldPos.z),
				physx::PxQuat(worldRotation.x, worldRotation.y, worldRotation.z, worldRotation.w));

			if (type == IRigidBody::BodyType::Static)
			{
				actor = g_pPhysx->GetPhysics()->createRigidStatic(localTm);
			}
			else
			{
				actor = g_pPhysx->GetPhysics()->createRigidDynamic(localTm);

				physx::PxRigidDynamic* dynamic = (physx::PxRigidDynamic*)actor;

				dynamic->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, type != BodyType::Dynamic);

			}

			if (actor)
			{
				_body = actor;

				if (_shape)
				{
					//auto localPose = _shape->getLocalPose();
					//_shape->setLocalPose(physx::PxTransform());
					_body->attachShape(*_shape);
				}

				actor->userData = this;// _transform->GetEntity();
				g_pPhysx->GetScene()->addActor(*actor);
			}

			if (_shape)
				_shape->release();

			g_pPhysx->GetScene()->unlockWrite();
		}
	}

	_bodyType = type;


}

HexEngine::IRigidBody::BodyType RigidBodyPhysX::GetBodyType() const
{
	return _bodyType;
}

void RigidBodyPhysX::UpdateBoxExtents(const dx::BoundingBox& box)
{
	g_pPhysx->GetScene()->lockWrite();

	if (_shape)
	{
		if (_shape->getGeometry().getType() == physx::PxGeometryType::eBOX)
		{
			physx::PxBoxGeometry geom;// = static_cast<const physx::PxBoxGeometry&>(_shape->getGeometry());

			_shape->setLocalPose(physx::PxTransform(physx::PxVec3(box.Center.x, box.Center.y, box.Center.z)));

			geom.halfExtents.x = std::max(box.Extents.x, 0.1f);
			geom.halfExtents.y = std::max(box.Extents.y, 0.1f);
			geom.halfExtents.z = std::max(box.Extents.z, 0.1f);

			_shape->setGeometry(geom);
		}
	}
	g_pPhysx->GetScene()->unlockWrite();
}

void RigidBodyPhysX::UpdateTriangleMeshScale(const math::Vector3& scale)
{
	g_pPhysx->GetScene()->lockWrite();

	if (_shape)
	{
		if (_shape->getGeometry().getType() == physx::PxGeometryType::eTRIANGLEMESH)
		{
			const physx::PxTriangleMeshGeometry& geom = static_cast<const physx::PxTriangleMeshGeometry&>(_shape->getGeometry());

			physx::PxTriangleMeshGeometry newGeom = geom;
			const math::Vector3 absoluteScale = _entity ? _entity->GetAbsoluteScale() : scale;
			newGeom.scale = BuildSafeTriangleMeshScale(absoluteScale);

			_shape->setGeometry(newGeom);
		}
	}
	g_pPhysx->GetScene()->unlockWrite();
}

void RigidBodyPhysX::UpdatePosePosition(const math::Vector3& position)
{
	g_pPhysx->GetScene()->lockWrite();
	//g_pPhysx->GetScene()->lockRead();

	auto currentPose = _body->getGlobalPose();
	currentPose.p = physx::PxVec3(position.x, position.y, position.z);
	_body->setGlobalPose(currentPose);

	//g_pPhysx->GetScene()->unlockRead();
	g_pPhysx->GetScene()->unlockWrite();
}

void RigidBodyPhysX::UpdatePoseRotation(const math::Quaternion& rotation)
{
	g_pPhysx->GetScene()->lockWrite();
	//g_pPhysx->GetScene()->lockRead();

	auto currentPose = _body->getGlobalPose();
	currentPose.q = physx::PxQuat(rotation.x, rotation.y, rotation.z, rotation.w);
	_body->setGlobalPose(currentPose);

	//g_pPhysx->GetScene()->unlockRead();
	g_pPhysx->GetScene()->unlockWrite();
}

math::Vector3 RigidBodyPhysX::GetPhysicsPosition()
{
	if (_collider)
	{
		auto shape = _collider->GetShape();

		if (shape)
		{
			g_pPhysx->GetScene()->lockRead();

			//auto pose = physx::PxShapeExt::getGlobalPose(*shape, *_body);
			//const physx::PxMat44 shapePose(physx::PxShapeExt::getGlobalPose(*shape, *_body));

			const auto physPos = _body->getGlobalPose().p; // Global/world position already.

			g_pPhysx->GetScene()->unlockRead();

			/*if (shape->getGeometryType() == physx::PxGeometryType::eBOX)
			{
				physx::PxBoxGeometry geom;
				shape->getBoxGeometry(geom);

				geom.halfExtents. *= _body->getGlobalPose();
			}*/


			return math::Vector3(physPos.x, physPos.y, physPos.z);
		}
	}

	return math::Vector3();
}

math::Quaternion RigidBodyPhysX::GetPhysicsRotation()
{
	if (_collider)
	{
		auto shape = _collider->GetShape();

		if (shape)
		{
			g_pPhysx->GetScene()->lockRead();

			auto pose = _body->getGlobalPose();// physx::PxShapeExt::getGlobalPose(*_collider->GetShape(), *_body);

			g_pPhysx->GetScene()->unlockRead();

			//const physx::PxMat44 shapePose();

			auto globalPose = pose.q;// _body->getGlobalPose().q;

			return math::Quaternion(globalPose.x, globalPose.y, globalPose.z, globalPose.w);
		}
	}

	return math::Quaternion::Identity;
}

math::Vector3 RigidBodyPhysX::GetLinearVelocity()
{
	if (_collider && _bodyType == BodyType::Dynamic)
	{
		auto shape = _collider->GetShape();

		if (shape)
		{
			g_pPhysx->GetScene()->lockRead();

			auto linearVel = ((physx::PxRigidBody*)_body)->getLinearVelocity();

			g_pPhysx->GetScene()->unlockRead();

			return math::Vector3(linearVel.x, linearVel.y, linearVel.z);
		}
	}

	return math::Vector3::Zero;
}

HexEngine::ICollider* RigidBodyPhysX::AddSphereCollider(HexEngine::Transform* transform, float radius)
{
	g_pPhysx->GetScene()->lockWrite();

	_geometry = new physx::PxSphereGeometry(radius);

	_shape = g_pPhysx->GetPhysics()->createShape(*(physx::PxSphereGeometry*)_geometry, *g_pPhysx->GetDefaultMaterial());

	_body->attachShape(*_shape);

	_shape->release();

	if (_bodyType == BodyType::Dynamic)
	{
		physx::PxRigidBodyExt::updateMassAndInertia(*(physx::PxRigidBody*)_body, GetMass());
	}

	_collider = new ColliderPhysX(_shape, this);

	g_pPhysx->GetScene()->unlockWrite();

	g_pPhysx->_resetDebug = true;

	return _collider;
}

HexEngine::ICollider* RigidBodyPhysX::AddCapsuleCollider(HexEngine::Transform* transform, float radius, float height)
{
	g_pPhysx->GetScene()->lockWrite();

	_geometry = new physx::PxCapsuleGeometry(radius, height / 2.0f);


	_shape = g_pPhysx->GetPhysics()->createShape(*(physx::PxCapsuleGeometry*)_geometry, *g_pPhysx->GetDefaultMaterial());

	_body->attachShape(*_shape);

	_shape->release();

	if (_bodyType == BodyType::Dynamic)
	{
		physx::PxRigidBodyExt::updateMassAndInertia(*(physx::PxRigidBody*)_body, GetMass());
	}

	_collider = new ColliderPhysX(_shape, this);

	g_pPhysx->GetScene()->unlockWrite();

	g_pPhysx->_resetDebug = true;

	return _collider;
}

void RigidBodyPhysX::RemoveCollider()
{
	g_pPhysx->GetScene()->lockWrite();

	if (_shape)
	{
		_body->detachShape(*_shape);
		_shape = nullptr;
		//_shape->release();
	}

	SAFE_DELETE(_collider);

	g_pPhysx->GetScene()->unlockWrite();

	g_pPhysx->_resetDebug = true;
}

#undef max

HexEngine::ICollider* RigidBodyPhysX::AddHeightFieldCollider(const int32_t columns, const int32_t rows, const float minHeight, const float maxHeight, float* heightValues, const math::Vector3& position, float scale)
{
	auto samples = new physx::PxHeightFieldSample[rows * columns];

	for (int i = 0; i < rows; i++)
	{
		for (int j = 0; j < columns; ++j)
		{
			float range = maxHeight - minHeight;

			auto& sample = samples[i * rows + j];

			int32_t x = i;// (rows - i) - 1;
			int32_t y = (columns - j) - 1;

			sample.height = (int16_t)(heightValues[y * rows + x]);// / (float)std::numeric_limits<int16_t>().max());
			//samples[i].materialIndex0 = 0;
			//samples[i].materialIndex1 = 0;
			sample.clearTessFlag();
		}
	}

	physx::PxHeightFieldDesc hfDesc;
	hfDesc.format = physx::PxHeightFieldFormat::eS16_TM;
	hfDesc.nbColumns = columns;
	hfDesc.nbRows = rows;
	hfDesc.samples.data = samples;
	hfDesc.samples.stride = sizeof(physx::PxHeightFieldSample);

	physx::PxDefaultMemoryOutputStream hfStream;
	PxCookHeightField(hfDesc, hfStream);

	physx::PxDefaultMemoryInputData input(hfStream.getData(), hfStream.getSize());
	physx::PxHeightField* aHeightField = g_pPhysx->GetPhysics()->createHeightField(input);

	/*physx::PxHeightField* aHeightField = g_pPhysx->GetCooking()->createHeightField(
		hfDesc,
		g_pPhysx->GetPhysics()->getPhysicsInsertionCallback());*/

	_geometry = new physx::PxHeightFieldGeometry(
		aHeightField,
		physx::PxMeshGeometryFlags(),
		1.0f/*(float)std::numeric_limits<int16_t>().max()*/,
		scale,
		scale);

	physx::PxHeightFieldGeometry* hfGeom = (physx::PxHeightFieldGeometry*)_geometry;

	_shape = physx::PxRigidActorExt::createExclusiveShape(
		*_body,
		*hfGeom,
		*g_pPhysx->GetDefaultMaterial());

	_shape->setLocalPose(physx::PxTransform(physx::PxVec3(-32.f * 32.f * 0.5f, 0.0f, -32.f * 32.f * 0.5f)));

	_collider = new ColliderPhysX(_shape, this);

	g_pPhysx->_resetDebug = true;

	delete[] samples;

	return _collider;
}

HexEngine::ICollider* RigidBodyPhysX::AddTriangleMeshCollider(const std::vector<math::Vector3>& vertices, const std::vector<HexEngine::MeshIndexFormat>& indices, uint32_t faceCount, bool exclusive)
{
	g_pPhysx->GetScene()->lockWrite();

	if (vertices.empty() || faceCount == 0 || indices.size() < static_cast<size_t>(faceCount) * 3)
	{
		LOG_CRIT("Invalid triangle mesh collider input: vertices=%zu indices=%zu faceCount=%u", vertices.size(), indices.size(), faceCount);
		g_pPhysx->GetScene()->unlockWrite();
		return nullptr;
	}

	physx::PxTriangleMeshDesc meshDesc;
	meshDesc.points.count = (uint32_t)vertices.size();
	meshDesc.points.stride = sizeof(physx::PxVec3);
	meshDesc.points.data = vertices.data();

	meshDesc.triangles.count = faceCount;
	meshDesc.triangles.stride = 3 * sizeof(HexEngine::MeshIndexFormat);
	meshDesc.triangles.data = indices.data();



	physx::PxTolerancesScale scale;
	physx::PxCookingParams params(scale);
	// Triangle meshes used by the character controller must be cleaned; otherwise a
	// single bad triangle can survive cooking and crash later in sweep tests.
	// Use a small weld tolerance to collapse near-duplicate vertices without
	// materially changing normal scene geometry.
	params.meshWeldTolerance = kTriangleMeshWeldTolerance;
	params.meshPreprocessParams |= physx::PxMeshPreprocessingFlag::eWELD_VERTICES;
	// disable edge precompute, edges are set for each triangle, slows contact generation
	params.meshPreprocessParams |= physx::PxMeshPreprocessingFlag::eDISABLE_ACTIVE_EDGES_PRECOMPUTE;
	// lower hierarchy for internal mesh
	//params.meshCookingHint = physx::PxMeshCookingHint::eCOOKING_PERFORMANCE;
	params.midphaseDesc.setToDefault(physx::PxMeshMidPhase::eBVH34);

	if (!PxValidateTriangleMesh(params, meshDesc))
	{
		LOG_WARN("Triangle mesh collider failed PhysX validation before cooking; attempting cleaned+welded cook anyway (weldTolerance=%f)", kTriangleMeshWeldTolerance);
	}

	//g_pPhysx->GetCooking()->setParams(params);

#if 0
	physx::PxDefaultMemoryOutputStream writeBuffer;
	physx::PxTriangleMeshCookingResult::Enum result;

	bool status = g_pPhysx->GetCooking()->cookTriangleMesh(meshDesc, writeBuffer, &result);

	if (!status)
		return NULL;

	physx::PxDefaultMemoryInputData readBuffer(writeBuffer.getData(), writeBuffer.getSize());
	auto mesh = g_pPhysx->GetPhysics()->createTriangleMesh(readBuffer);
#else
	physx::PxDefaultMemoryOutputStream buf;
	physx::PxTriangleMeshCookingResult::Enum result = physx::PxTriangleMeshCookingResult::eFAILURE;
	if (!PxCookTriangleMesh(params, meshDesc, buf, &result))
	{
		LOG_CRIT("PxCookTriangleMesh failed for collider mesh (result=%u, vertices=%zu, faces=%u)", static_cast<uint32_t>(result), vertices.size(), faceCount);
		g_pPhysx->GetScene()->unlockWrite();
		return nullptr;
	}

	physx::PxDefaultMemoryInputData input(buf.getData(), buf.getSize());
	physx::PxTriangleMesh* mesh = g_pPhysx->GetPhysics()->createTriangleMesh(input);
	if (mesh == nullptr)
	{
		LOG_CRIT("createTriangleMesh() returned null after successful cooking");
		g_pPhysx->GetScene()->unlockWrite();
		return nullptr;
	}

	//auto mesh = g_pPhysx->GetCooking()->createTriangleMesh(meshDesc, g_pPhysx->GetPhysics()->getPhysicsInsertionCallback());
#endif
	_geometry = new physx::PxTriangleMeshGeometry;

	physx::PxTriangleMeshGeometry* triGeom = (physx::PxTriangleMeshGeometry*)_geometry;

	const math::Vector3 realScale = _entity ? _entity->GetAbsoluteScale() : _transform->GetScale();

	triGeom->triangleMesh = mesh;
	triGeom->scale = BuildSafeTriangleMeshScale(realScale);

	_shape = g_pPhysx->GetPhysics()->createShape(*triGeom, *g_pPhysx->GetDefaultMaterial(), exclusive);

	if (_shape == nullptr)
	{
		LOG_CRIT("createShape() returned null!");

		g_pPhysx->GetScene()->unlockWrite();

		return nullptr;
	}

	_body->attachShape(*_shape);
	_shape->release();

	/*_shape = physx::PxRigidActorExt::createExclusiveShape(
		*_body,
		*triGeom,
		*g_pPhysx->GetDefaultMaterial());*/

		// we don't want to handle scene queries with physx
		//_shape->setFlag(physx::PxShapeFlag::eSCENE_QUERY_SHAPE, false);

	_collider = new ColliderPhysX(_shape, this);

	g_pPhysx->GetScene()->unlockWrite();

	g_pPhysx->_resetDebug = true;

	return _collider;
}

bool RigidBodyPhysX::BeginAddTriangleMeshColliderAsync(
	const std::vector<math::Vector3>& vertices,
	const std::vector<HexEngine::MeshIndexFormat>& indices,
	uint32_t faceCount,
	bool exclusive)
{
	if (_asyncCookInFlight.load(std::memory_order_acquire))
		return false;
	if (vertices.empty() || faceCount == 0 || indices.size() < static_cast<size_t>(faceCount) * 3)
		return false;

	// Snapshot the inputs so the worker thread sees a stable copy even if the
	// caller frees / rebuilds the source vectors before the cook completes.
	auto input = std::make_shared<AsyncTriMeshCookInput>();
	input->vertices = vertices;
	input->indices = indices;
	input->faceCount = faceCount;
	input->exclusive = exclusive;
	_asyncCookInput = input;
	_asyncCookInFlight.store(true, std::memory_order_release);
	// Discard any previously-cached cooked buffer - it's about to be
	// replaced by the result of this cook. Don't keep a stale snapshot
	// from a previous Begin/TryFinish cycle.
	_lastCookedBuffer.clear();

	_asyncCookFuture = std::async(std::launch::async, [input]()
	{
		AsyncTriMeshCookOutput out;

		physx::PxTriangleMeshDesc meshDesc;
		meshDesc.points.count = static_cast<uint32_t>(input->vertices.size());
		meshDesc.points.stride = sizeof(physx::PxVec3);
		meshDesc.points.data = input->vertices.data();

		meshDesc.triangles.count = input->faceCount;
		meshDesc.triangles.stride = 3 * sizeof(HexEngine::MeshIndexFormat);
		meshDesc.triangles.data = input->indices.data();

		physx::PxTolerancesScale scale;
		physx::PxCookingParams params(scale);
		params.meshWeldTolerance = kTriangleMeshWeldTolerance;
		params.meshPreprocessParams |= physx::PxMeshPreprocessingFlag::eWELD_VERTICES;
		params.meshPreprocessParams |= physx::PxMeshPreprocessingFlag::eDISABLE_ACTIVE_EDGES_PRECOMPUTE;
		params.midphaseDesc.setToDefault(physx::PxMeshMidPhase::eBVH34);

		// PxCookTriangleMesh is documented as thread-safe (it's a pure
		// algorithmic function operating on the input meshDesc) so multiple
		// workers can cook in parallel without coordination.
		physx::PxDefaultMemoryOutputStream buf;
		physx::PxTriangleMeshCookingResult::Enum result = physx::PxTriangleMeshCookingResult::eFAILURE;
		if (!PxCookTriangleMesh(params, meshDesc, buf, &result))
		{
			out.success = false;
			return out;
		}

		out.cookedBuffer.assign(buf.getData(), buf.getData() + buf.getSize());
		out.success = true;
		return out;
	});

	return true;
}

bool RigidBodyPhysX::HasAsyncColliderInFlight() const
{
	return _asyncCookInFlight.load(std::memory_order_acquire);
}

HexEngine::ICollider* RigidBodyPhysX::AddTriangleMeshColliderFromCookedBuffer(const std::vector<uint8_t>& cookedBuffer, bool exclusive)
{
	if (cookedBuffer.empty())
		return nullptr;

	g_pPhysx->GetScene()->lockWrite();

	physx::PxDefaultMemoryInputData input(
		const_cast<uint8_t*>(cookedBuffer.data()),
		static_cast<uint32_t>(cookedBuffer.size()));
	physx::PxTriangleMesh* mesh = g_pPhysx->GetPhysics()->createTriangleMesh(input);
	if (mesh == nullptr)
	{
		LOG_CRIT("createTriangleMesh() returned null from cached cooked buffer (size=%zu)", cookedBuffer.size());
		g_pPhysx->GetScene()->unlockWrite();
		return nullptr;
	}

	_geometry = new physx::PxTriangleMeshGeometry;
	auto* triGeom = static_cast<physx::PxTriangleMeshGeometry*>(_geometry);

	const math::Vector3 realScale = _entity ? _entity->GetAbsoluteScale() : _transform->GetScale();
	triGeom->triangleMesh = mesh;
	triGeom->scale = BuildSafeTriangleMeshScale(realScale);

	_shape = g_pPhysx->GetPhysics()->createShape(*triGeom, *g_pPhysx->GetDefaultMaterial(), exclusive);
	if (_shape == nullptr)
	{
		LOG_CRIT("createShape() returned null from cached cooked buffer");
		g_pPhysx->GetScene()->unlockWrite();
		return nullptr;
	}

	_body->attachShape(*_shape);
	_shape->release();

	_collider = new ColliderPhysX(_shape, this);

	g_pPhysx->GetScene()->unlockWrite();
	g_pPhysx->_resetDebug = true;
	return _collider;
}

bool RigidBodyPhysX::TryFinishAsyncCollider()
{
	if (!_asyncCookInFlight.load(std::memory_order_acquire))
		return false;
	if (!_asyncCookFuture.valid())
	{
		// Defensive: state desynced. Clear the flag so we don't loop forever.
		_asyncCookInFlight.store(false, std::memory_order_release);
		_asyncCookInput.reset();
		return false;
	}

	// Poll without blocking. Frame budget here is "the main thread checks
	// once per Update tick and only finalises if the worker has finished" -
	// if the cook isn't done yet, return immediately and try again next
	// frame.
	if (_asyncCookFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
		return false;

	AsyncTriMeshCookOutput cooked = _asyncCookFuture.get();
	const bool exclusive = _asyncCookInput ? _asyncCookInput->exclusive : true;
	_asyncCookInput.reset();
	_asyncCookInFlight.store(false, std::memory_order_release);

	if (!cooked.success)
	{
		LOG_CRIT("Async triangle mesh cook failed");
		return false;
	}

	// Stash the cooked bytes so the caller can save them for next-load reuse.
	_lastCookedBuffer = cooked.cookedBuffer;

	// Finalise on the main thread - createTriangleMesh / createShape /
	// attachShape all touch the PhysX device + scene and aren't safe to
	// invoke from the worker.
	g_pPhysx->GetScene()->lockWrite();

	physx::PxDefaultMemoryInputData input(cooked.cookedBuffer.data(), static_cast<uint32_t>(cooked.cookedBuffer.size()));
	physx::PxTriangleMesh* mesh = g_pPhysx->GetPhysics()->createTriangleMesh(input);
	if (mesh == nullptr)
	{
		LOG_CRIT("createTriangleMesh() returned null after successful async cook");
		g_pPhysx->GetScene()->unlockWrite();
		return false;
	}

	_geometry = new physx::PxTriangleMeshGeometry;
	auto* triGeom = static_cast<physx::PxTriangleMeshGeometry*>(_geometry);

	const math::Vector3 realScale = _entity ? _entity->GetAbsoluteScale() : _transform->GetScale();
	triGeom->triangleMesh = mesh;
	triGeom->scale = BuildSafeTriangleMeshScale(realScale);

	_shape = g_pPhysx->GetPhysics()->createShape(*triGeom, *g_pPhysx->GetDefaultMaterial(), exclusive);
	if (_shape == nullptr)
	{
		LOG_CRIT("createShape() returned null in async finalise");
		g_pPhysx->GetScene()->unlockWrite();
		return false;
	}

	_body->attachShape(*_shape);
	_shape->release();

	_collider = new ColliderPhysX(_shape, this);

	g_pPhysx->GetScene()->unlockWrite();
	g_pPhysx->_resetDebug = true;
	return true;
}

HexEngine::ICollider* RigidBodyPhysX::AddConvexMeshCollider(const std::vector<math::Vector3>& vertices, const std::vector<HexEngine::MeshIndexFormat>& indices, uint32_t faceCount, bool exclusive)
{
	g_pPhysx->GetScene()->lockWrite();

	physx::PxConvexMeshDesc meshDesc;
	meshDesc.points.count = (uint32_t)vertices.size();
	meshDesc.points.stride = sizeof(physx::PxVec3);
	meshDesc.points.data = vertices.data();

	meshDesc.indices.count = faceCount;
	meshDesc.indices.stride = 3 * sizeof(HexEngine::MeshIndexFormat);
	meshDesc.indices.data = indices.data();

	meshDesc.flags = physx::PxConvexFlag::eCOMPUTE_CONVEX | physx::PxConvexFlag::eDISABLE_MESH_VALIDATION | physx::PxConvexFlag::eFAST_INERTIA_COMPUTATION;


	/*physx::PxDefaultMemoryOutputStream buf;
	physx::PxConvexMeshCookingResult::Enum result;
	if (!g_pPhysx->GetCooking()->cookConvexMesh(meshDesc, buf, &result))
		return NULL;
	physx::PxDefaultMemoryInputData input(buf.getData(), buf.getSize());
	physx::PxConvexMesh* convexMesh = g_pPhysx->GetPhysics()->createConvexMesh(input);*/

	/*
	physx::PxDefaultMemoryOutputStream writeBuffer;
	physx::PxTriangleMeshCookingResult::Enum result;

	bool status = g_pPhysx->GetCooking()->cookTriangleMesh(meshDesc, writeBuffer, &result);

	if (!status)
		return NULL;

	physx::PxDefaultMemoryInputData readBuffer(writeBuffer.getData(), writeBuffer.getSize());
	auto mesh = g_pPhysx->GetPhysics()->createTriangleMesh(readBuffer);*/

	physx::PxTolerancesScale scale;
	physx::PxCookingParams params(scale);
	// disable mesh cleaning - perform mesh validation on development configurations
	params.meshPreprocessParams |= physx::PxMeshPreprocessingFlag::eDISABLE_CLEAN_MESH;
	// disable edge precompute, edges are set for each triangle, slows contact generation
	params.meshPreprocessParams |= physx::PxMeshPreprocessingFlag::eDISABLE_ACTIVE_EDGES_PRECOMPUTE;
	// lower hierarchy for internal mesh
	//params.meshCookingHint = physx::PxMeshCookingHint::eCOOKING_PERFORMANCE;
	params.midphaseDesc.setToDefault(physx::PxMeshMidPhase::eBVH34);

	physx::PxDefaultMemoryOutputStream buf;
	PxCookConvexMesh(params, meshDesc, buf);

	physx::PxDefaultMemoryInputData input(buf.getData(), buf.getSize());
	physx::PxConvexMesh* mesh = g_pPhysx->GetPhysics()->createConvexMesh(input);

	//auto mesh = g_pPhysx->GetCooking()->createConvexMesh(meshDesc, g_pPhysx->GetPhysics()->getPhysicsInsertionCallback());

	_geometry = new physx::PxConvexMeshGeometry;

	physx::PxConvexMeshGeometry* triGeom = (physx::PxConvexMeshGeometry*)_geometry;

	triGeom->convexMesh = mesh;


	/*_shape = physx::PxRigidActorExt::createExclusiveShape(
		*_body,
		*triGeom,
		*g_pPhysx->GetDefaultMaterial());*/

	_shape = g_pPhysx->GetPhysics()->createShape(*triGeom, *g_pPhysx->GetDefaultMaterial(), exclusive);
	_body->attachShape(*_shape);
	_shape->release();

	// we don't want to handle scene queries with physx
	//_shape->setFlag(physx::PxShapeFlag::eSCENE_QUERY_SHAPE, false);

	_collider = new ColliderPhysX(_shape, this);

	g_pPhysx->GetScene()->unlockWrite();

	g_pPhysx->_resetDebug = true;

	return _collider;
}

HexEngine::ICollider* RigidBodyPhysX::AddBoxCollider(const dx::BoundingBox& box, bool exclusive)
{
	g_pPhysx->GetScene()->lockWrite();

	//_shape = g_pPhysx->GetPhysics()->createShape(physx::PxBoxGeometry(box.Extents.x + box.Center.x, box.Extents.y * 0.5f + box.Center.y, box.Extents.z + box.Center.z), *g_pPhysx->GetDefaultMaterial());

	_geometry = new physx::PxBoxGeometry(std::max(box.Extents.x, 0.1f), std::max(box.Extents.y, 0.1f), std::max(box.Extents.z, 0.1f));

	_shape = g_pPhysx->GetPhysics()->createShape(*(physx::PxBoxGeometry*)_geometry, *g_pPhysx->GetDefaultMaterial(), exclusive);

	_shape->setLocalPose(physx::PxTransform(physx::PxVec3(box.Center.x, box.Center.y, box.Center.z)));

	_body->attachShape(*_shape);

	_shape->release();

	/*auto globalPose = _body->getGlobalPose();

	globalPose.p -= physx::PxVec3(0.0f, box.Extents.y, 0.0f);

	_body->setGlobalPose(globalPose);	*/

	if (_bodyType == BodyType::Dynamic)
	{
		physx::PxRigidBodyExt::updateMassAndInertia(*(physx::PxRigidBody*)_body, GetMass());
	}

	_collider = new ColliderPhysX(_shape, this);

	g_pPhysx->GetScene()->unlockWrite();

	g_pPhysx->_resetDebug = true;

	return _collider;
}

void RigidBodyPhysX::ApplyForceToCenterOfMass(const math::Vector3& force)
{
	if (_bodyType == BodyType::Dynamic || _bodyType == BodyType::Kinematic)
	{
		g_pPhysx->GetScene()->lockWrite();

		((physx::PxRigidDynamic*)_body)->addForce(*(physx::PxVec3*)&force.x);

		g_pPhysx->GetScene()->unlockWrite();
	}
}

void RigidBodyPhysX::ApplyTorque(const math::Vector3& torque)
{
	if (_bodyType == BodyType::Dynamic || _bodyType == BodyType::Kinematic)
	{
		g_pPhysx->GetScene()->lockWrite();

		((physx::PxRigidDynamic*)_body)->addTorque(*(physx::PxVec3*)&torque.x);

		g_pPhysx->GetScene()->unlockWrite();
	}
}

void RigidBodyPhysX::SetMass(float mass)
{
	if (_bodyType == BodyType::Dynamic/* || _bodyType == BodyType::Kinematic*/)
	{
		g_pPhysx->GetScene()->lockWrite();

		physx::PxRigidBodyExt::setMassAndUpdateInertia(*(physx::PxRigidDynamic*)_body, mass);

		g_pPhysx->GetScene()->unlockWrite();
	}

	_mass = mass;
}

float RigidBodyPhysX::GetMass() const
{
	return _mass;

	/*float mass = 1.0f;

	if (_bodyType == BodyType::Dynamic)
	{
		g_pPhysx->GetScene()->lockRead();

		mass = ((physx::PxRigidDynamic*)_body)->getMass();

		g_pPhysx->GetScene()->unlockRead();
	}
	return mass;*/
}

float RigidBodyPhysX::GetMaxAngularVelocity()
{
	float vel = 0.0f;

	if (_bodyType == BodyType::Dynamic || _bodyType == BodyType::Kinematic)
	{
		g_pPhysx->GetScene()->lockRead();

		vel = ((physx::PxRigidDynamic*)_body)->getMaxAngularVelocity();

		g_pPhysx->GetScene()->unlockRead();
	}

	return vel;
}
void RigidBodyPhysX::SetMaxAngularVelocity(float maxVel)
{
	if (_bodyType == BodyType::Dynamic || _bodyType == BodyType::Kinematic)
	{
		g_pPhysx->GetScene()->lockWrite();

		((physx::PxRigidDynamic*)_body)->setMaxAngularVelocity(maxVel);

		g_pPhysx->GetScene()->unlockWrite();
	}
}

float RigidBodyPhysX::GetMaxLinearVelocity()
{
	float vel = 0.0f;

	if (_bodyType == BodyType::Dynamic || _bodyType == BodyType::Kinematic)
	{
		g_pPhysx->GetScene()->lockRead();

		vel = ((physx::PxRigidDynamic*)_body)->getMaxLinearVelocity();

		g_pPhysx->GetScene()->unlockRead();
	}

	return vel;
}
void RigidBodyPhysX::SetMaxLinearVelocity(float maxVel)
{
	if (_bodyType == BodyType::Dynamic || _bodyType == BodyType::Kinematic)
	{
		g_pPhysx->GetScene()->lockWrite();

		((physx::PxRigidDynamic*)_body)->setMaxLinearVelocity(maxVel);

		g_pPhysx->GetScene()->unlockWrite();
	}
}

void RigidBodyPhysX::SetGravityEnabled(bool enabled, bool resetVelocity)
{
	if (_bodyType == BodyType::Dynamic || _bodyType == BodyType::Kinematic)
	{
		g_pPhysx->GetScene()->lockWrite();

		_body->setActorFlag(physx::PxActorFlag::eDISABLE_GRAVITY, !enabled);

		if (!enabled && resetVelocity)
		{
			((physx::PxRigidDynamic*)_body)->setLinearVelocity(physx::PxVec3(physx::PxZero));
			((physx::PxRigidDynamic*)_body)->setAngularVelocity(physx::PxVec3(physx::PxZero));
		}

		g_pPhysx->GetScene()->unlockWrite();
	}
}

void RigidBodyPhysX::OnCollision(IRigidBody* body)
{
}

void RigidBodyPhysX::SetPhysicalProperties(const PhysicalProperties& props)
{
	if (_bodyType == BodyType::Dynamic || _bodyType == BodyType::Kinematic)
	{
		g_pPhysx->GetScene()->lockWrite();

		if (_customMaterial)
			_customMaterial->release();

		_customMaterial = g_pPhysx->GetPhysics()->createMaterial(props.staticFriction, props.dynamicFriction, props.restitution);

		_shape->setMaterials(&_customMaterial, 1);

		g_pPhysx->GetScene()->unlockWrite();
	}
}

void RigidBodyPhysX::SetLinearVelocityDamping(float damping)
{
	if (_bodyType == BodyType::Dynamic || _bodyType == BodyType::Kinematic)
	{
		g_pPhysx->GetScene()->lockWrite();

		((physx::PxRigidDynamic*)_body)->setLinearDamping(damping);

		g_pPhysx->GetScene()->unlockWrite();
	}
}

void RigidBodyPhysX::SetAngularVelocityDamping(float damping)
{
	if (_bodyType == BodyType::Dynamic || _bodyType == BodyType::Kinematic)
	{
		g_pPhysx->GetScene()->lockWrite();

		((physx::PxRigidDynamic*)_body)->setAngularDamping(damping);

		g_pPhysx->GetScene()->unlockWrite();
	}
}

HexEngine::ICollider* RigidBodyPhysX::GetICollider()
{
	return _collider;
}

HexEngine::Transform* RigidBodyPhysX::GetTransform()
{
	return _transform;
}

physx::PxRigidActor* RigidBodyPhysX::GetRigidActor()
{
	return _body;
}

void RigidBodyPhysX::Move(const math::Vector3& dir, float minLength, float frameTime)
{
	ApplyForceToCenterOfMass(dir);
}

void RigidBodyPhysX::WakeUp()
{
	if (_bodyType == BodyType::Dynamic)
	{
		g_pPhysx->GetScene()->lockWrite();

		((physx::PxRigidDynamic*)_body)->wakeUp();

		g_pPhysx->GetScene()->unlockWrite();
	}
}

void RigidBodyPhysX::PutToSleep()
{
	if (_bodyType == BodyType::Dynamic)
	{
		g_pPhysx->GetScene()->lockWrite();

		((physx::PxRigidDynamic*)_body)->putToSleep();

		g_pPhysx->GetScene()->unlockWrite();
	}
}

void RigidBodyPhysX::SetIsTrigger(bool istrigger)
{
	if (_shape)
	{
		// Shared shapes can't be made into triggers, so fix it
		if (_shape->isExclusive() == false && _shape->getReferenceCount() > 1)
		{

		}
		if (istrigger)
		{
			_shape->setFlag(physx::PxShapeFlag::eSIMULATION_SHAPE, false);
			_shape->setFlag(physx::PxShapeFlag::eTRIGGER_SHAPE, true);
		}
		else
		{
			_shape->setFlag(physx::PxShapeFlag::eTRIGGER_SHAPE, false);
			_shape->setFlag(physx::PxShapeFlag::eSIMULATION_SHAPE, true);
		}
	}
}

bool RigidBodyPhysX::GetIsTrigger()
{
	if (_shape)
	{
		return _shape->getFlags().isSet(physx::PxShapeFlag::eTRIGGER_SHAPE);
	}
	return false;
}

void RigidBodyPhysX::SetIsSimulated(bool simulated)
{
	_body->setActorFlag(physx::PxActorFlag::eDISABLE_SIMULATION, !simulated);
	//_shape->setFlag(physx::PxShapeFlag::eSIMULATION_SHAPE, !simulation
}

bool RigidBodyPhysX::GetIsSimulated()
{
	return _body->getActorFlags().isSet(physx::PxActorFlag::eDISABLE_SIMULATION) == false;
}
