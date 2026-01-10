

#include "RigidBodyPhysX.hpp"
#include "PhysicsSystemPhysX.hpp"
#include "ColliderPhysX.hpp"
#include <HexEngine.Core/Environment/LogFile.hpp>
#include <HexEngine.Core/Entity/Entity.hpp>
#include <HexEngine.Core/Entity/Component/RigidBody.hpp>

namespace HexEngine
{
	RigidBodyPhysX::RigidBodyPhysX(physx::PxRigidActor* body, RigidBody* bodyComponent, Entity* entity) :
		_body(body),
		_bodyComponent(bodyComponent),
		_entity(entity)
	{
	}

	void RigidBodyPhysX::Create(Transform* transform)
	{
		_transform = transform;
	}

	void RigidBodyPhysX::Destroy()
	{
		if(_shape)
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

				if(_shape)
					_shape->acquireReference();
				//_body->detachShape(*_shape);

				PX_RELEASE(_body);				

				physx::PxRigidActor* actor = nullptr;

				auto position = _transform->GetPosition();

				physx::PxTransform localTm(*(physx::PxVec3*)&position.x);

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

				if(_shape)
					_shape->release();

				g_pPhysx->GetScene()->unlockWrite();
			}
		}

		_bodyType = type;

		
	}

	IRigidBody::BodyType RigidBodyPhysX::GetBodyType() const
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

				geom.halfExtents.x = box.Extents.x;
				geom.halfExtents.y = box.Extents.y;
				geom.halfExtents.z = box.Extents.z;

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

				newGeom.scale.scale.x = scale.x;
				newGeom.scale.scale.y = scale.y;
				newGeom.scale.scale.z = scale.z;

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

				auto physPos = _body->getGlobalPose().p; //shapePose.getPosition();

				auto parent = GetEntity()->GetParent();

				while (parent)
				{
					if (auto rb = parent->GetComponent<RigidBody>(); rb != nullptr)
					{
						auto rbParent = (RigidBodyPhysX*)rb->GetIRigidBody();
						auto parentPose = rbParent->_body->getGlobalPose();

						physPos += parentPose.p;
					}

					parent = parent->GetParent();
				}

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

	ICollider* RigidBodyPhysX::AddSphereCollider(Transform* transform, float radius)
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

	ICollider* RigidBodyPhysX::AddCapsuleCollider(Transform* transform, float radius, float height)
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

	ICollider* RigidBodyPhysX::AddHeightFieldCollider(const int32_t columns, const int32_t rows, const float minHeight, const float maxHeight, float* heightValues, const math::Vector3& position, float scale)
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

	ICollider* RigidBodyPhysX::AddTriangleMeshCollider(const std::vector<math::Vector3>& vertices, const std::vector<MeshIndexFormat>& indices, uint32_t faceCount, bool exclusive)
	{
		g_pPhysx->GetScene()->lockWrite();

		physx::PxTriangleMeshDesc meshDesc;
		meshDesc.points.count = (uint32_t)vertices.size();
		meshDesc.points.stride = sizeof(physx::PxVec3);
		meshDesc.points.data = vertices.data();

		meshDesc.triangles.count = faceCount;
		meshDesc.triangles.stride = 3 * sizeof(MeshIndexFormat);
		meshDesc.triangles.data = indices.data();		

		

		physx::PxTolerancesScale scale;
		physx::PxCookingParams params(scale);
		// disable mesh cleaning - perform mesh validation on development configurations
		params.meshPreprocessParams |= physx::PxMeshPreprocessingFlag::eDISABLE_CLEAN_MESH;
		// disable edge precompute, edges are set for each triangle, slows contact generation
		params.meshPreprocessParams |= physx::PxMeshPreprocessingFlag::eDISABLE_ACTIVE_EDGES_PRECOMPUTE;
		// lower hierarchy for internal mesh
		//params.meshCookingHint = physx::PxMeshCookingHint::eCOOKING_PERFORMANCE;
		params.midphaseDesc.setToDefault(physx::PxMeshMidPhase::eBVH34);

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
		PxCookTriangleMesh(params, meshDesc, buf);

		physx::PxDefaultMemoryInputData input(buf.getData(), buf.getSize());
		physx::PxTriangleMesh* mesh = g_pPhysx->GetPhysics()->createTriangleMesh(input);

		//auto mesh = g_pPhysx->GetCooking()->createTriangleMesh(meshDesc, g_pPhysx->GetPhysics()->getPhysicsInsertionCallback());
#endif
		_geometry = new physx::PxTriangleMeshGeometry;

		physx::PxTriangleMeshGeometry* triGeom = (physx::PxTriangleMeshGeometry*)_geometry;

		auto realScale = _transform->GetScale();

		auto parent = GetEntity()->GetParent();
		while (parent)
		{
			realScale *= parent->GetComponent<Transform>()->GetScale();
			parent = parent->GetParent();
		}

		triGeom->triangleMesh = mesh;
		triGeom->scale = physx::PxMeshScale(*(physx::PxVec3*)&realScale.x);

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

	ICollider* RigidBodyPhysX::AddConvexMeshCollider(const std::vector<math::Vector3>& vertices, const std::vector<MeshIndexFormat>& indices, uint32_t faceCount, bool exclusive)
	{
		g_pPhysx->GetScene()->lockWrite();

		physx::PxConvexMeshDesc meshDesc;
		meshDesc.points.count = (uint32_t)vertices.size();
		meshDesc.points.stride = sizeof(physx::PxVec3);
		meshDesc.points.data = vertices.data();

		meshDesc.indices.count = faceCount;
		meshDesc.indices.stride = 3 * sizeof(MeshIndexFormat);
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

	ICollider* RigidBodyPhysX::AddBoxCollider(const dx::BoundingBox& box, bool exclusive)
	{
		g_pPhysx->GetScene()->lockWrite();

		//_shape = g_pPhysx->GetPhysics()->createShape(physx::PxBoxGeometry(box.Extents.x + box.Center.x, box.Extents.y * 0.5f + box.Center.y, box.Extents.z + box.Center.z), *g_pPhysx->GetDefaultMaterial());

		_geometry = new physx::PxBoxGeometry(box.Extents.x, box.Extents.y, box.Extents.z);
		
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

	ICollider* RigidBodyPhysX::GetICollider()
	{
		return _collider;
	}

	Transform* RigidBodyPhysX::GetTransform()
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
				_shape->setFlag(physx::PxShapeFlag::eSIMULATION_SHAPE, true);
				_shape->setFlag(physx::PxShapeFlag::eTRIGGER_SHAPE, false);
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
}