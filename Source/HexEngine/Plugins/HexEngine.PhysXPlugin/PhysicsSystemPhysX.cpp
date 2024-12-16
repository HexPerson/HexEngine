

#include "PhysicsSystemPhysX.hpp"
#include "RigidBodyPhysX.hpp"
#include <HexEngine.Core/HexEngine.hpp>
#include <extensions\PxExtensionsAPI.h>

namespace HexEngine
{
	physx::PxFilterFlags SampleSubmarineFilterShader(
		physx::PxFilterObjectAttributes attributes0, physx::PxFilterData filterData0,
		physx::PxFilterObjectAttributes attributes1, physx::PxFilterData filterData1,
		physx::PxPairFlags& pairFlags, const void* constantBlock, physx::PxU32 constantBlockSize)
	{
		// let triggers through
		if (physx::PxFilterObjectIsTrigger(attributes0) || physx::PxFilterObjectIsTrigger(attributes1))
		{
			pairFlags = physx::PxPairFlag::eTRIGGER_DEFAULT;
			return physx::PxFilterFlag::eDEFAULT;
		}
		// generate contacts for all that were not filtered above
		pairFlags = physx::PxPairFlag::eCONTACT_DEFAULT;

		// trigger the contact callback for pairs (A,B) where
		// the filtermask of A contains the ID of B and vice versa.
		//if ((filterData0.word0 & filterData1.word1) && (filterData1.word0 & filterData0.word1))
			pairFlags |= physx::PxPairFlag::eNOTIFY_TOUCH_FOUND;

		return physx::PxFilterFlag::eDEFAULT;
	}

	void PhysicsSystemPhysX::reportError(physx::PxErrorCode::Enum code, const char* message, const char* file, int line)
	{
		LOG_CRIT("PhysX Error: %s\n%s(%d)", message, file, line);
	}

	bool PhysicsSystemPhysX::Create()
	{
		g_pPhysx = this;

		static physx::PxDefaultErrorCallback gDefaultErrorCallback;
		static physx::PxDefaultAllocator gDefaultAllocatorCallback;

		_foundation = PxCreateFoundation(PX_PHYSICS_VERSION, gDefaultAllocatorCallback,	*this);

#ifdef _DEBUG
		_pvd = PxCreatePvd(*_foundation);

		physx::PxPvdTransport* transport = physx::PxDefaultPvdSocketTransportCreate("127.0.0.1", 5425, 10);
		_pvd->connect(*transport, physx::PxPvdInstrumentationFlag::eALL);

		bool recordMemoryAllocations = true;
#else

		bool recordMemoryAllocations = false;
#endif

		_physics = PxCreatePhysics(PX_PHYSICS_VERSION, *_foundation, physx::PxTolerancesScale(), recordMemoryAllocations, _pvd);

		PxInitExtensions(*_physics, _pvd);

		// Create a physics scene
		//
		physx::PxSceneDesc sceneDesc(_physics->getTolerancesScale());

		_dispatcher = physx::PxDefaultCpuDispatcherCreate(2);

		sceneDesc.gravity = physx::PxVec3(0.0f, -98.1f, 0.0f);
		sceneDesc.cpuDispatcher = _dispatcher;
		sceneDesc.filterShader = SampleSubmarineFilterShader;// physx::PxDefaultSimulationFilterShader;
		sceneDesc.simulationEventCallback = this;
		//sceneDesc.flags |= physx::PxSceneFlag::eENABLE_PCM;
		//sceneDesc.broadPhaseType = physx::PxBroadPhaseType::eSAP;
		//sceneDesc.flags.set(0);// = (physx::PxSceneFlag)0;// physx::PxSceneFlag::eREQUIRE_RW_LOCK | physx::PxSceneFlag::eENABLE_GPU_DYNAMICS;

		_scene = _physics->createScene(sceneDesc);

#ifdef _DEBUG
		_scene->lockWrite();
		//_scene->setVisualizationParameter(physx::PxVisualizationParameter::eSCALE, 1.0f);
		//_scene->setVisualizationParameter(physx::PxVisualizationParameter::eCOLLISION_STATIC, 1.0f);
		// _scene->setVisualizationParameter(physx::PxVisualizationParameter::eCOLLISION_DYNAMIC, 1.0f);
		//_scene->setVisualizationParameter(physx::PxVisualizationParameter::eCOLLISION_AABBS, 1.0f);
		//_scene->setVisualizationParameter(physx::PxVisualizationParameter::eCOLLISION_SHAPES, 1.0f);
		_scene->unlockWrite();
#endif

		

		_cooking = PxCreateCooking(PX_PHYSICS_VERSION, *_foundation, physx::PxCookingParams(_physics->getTolerancesScale()));
		//if (!_cooking)
		//	fatalError("PxCreateCooking failed!");

		_controllerManager = PxCreateControllerManager(*_scene);


		// create a default material
		//
		_defaultMaterial = _physics->createMaterial(0.5f, 0.5f, 0.6f);

		/*PxInitVehicleSDK(*_physics);
		PxVehicleSetBasisVectors(physx::PxVec3(0, 1, 0), physx::PxVec3(0, 0, 1));
		PxVehicleSetUpdateMode(physx::PxVehicleUpdateMode::eVELOCITY_CHANGE);*/

		return true;
	}

	void PhysicsSystemPhysX::Destroy()
	{
		PxCloseExtensions();

		PX_RELEASE(_controllerManager);
		PX_RELEASE(_defaultMaterial);
		PX_RELEASE(_cooking);
		PX_RELEASE(_scene);
		PX_RELEASE(_dispatcher);
		PX_RELEASE(_physics);
		PX_RELEASE(_pvd);
		PX_RELEASE(_foundation);
	}

	math::Vector3 PhysicsSystemPhysX::GetGravity()
	{
		const auto& sceneGrav = _scene->getGravity();
		return math::Vector3(sceneGrav.x, sceneGrav.y, sceneGrav.z);
	}

	void PhysicsSystemPhysX::SetGravity(const math::Vector3& gravity)
	{
		_scene->setGravity(physx::PxVec3(gravity.x, gravity.y, gravity.z));
	}

	IRigidBody* PhysicsSystemPhysX::CreateController(const ControllerParameters& params, Transform* transform)
	{
		_scene->lockWrite();

		physx::PxCapsuleControllerDesc desc;
		desc.climbingMode = physx::PxCapsuleClimbingMode::eEASY;
		desc.contactOffset = 0.04f;
		desc.density = params.density;
		desc.height = params.height;
		desc.invisibleWallHeight = 0.0f;
		desc.material = _defaultMaterial;
		desc.maxJumpHeight = 0.0f;
		desc.nonWalkableMode = physx::PxControllerNonWalkableMode::ePREVENT_CLIMBING;
		desc.position = physx::PxExtendedVec3(params.initialPosition.x, params.initialPosition.y + (params.height * 0.5f) + params.radius, params.initialPosition.z);
		desc.radius = params.radius;
		desc.slopeLimit = cosf(params.maxSlope);
		desc.stepOffset = params.stepOffset;
		desc.density = params.density;

		desc.reportCallback = this;

		auto controller = _controllerManager->createController(desc);

		CharacterController* character = new CharacterController(controller, controller->getActor(), transform->GetEntity());

		character->Create(transform);
		character->SetBodyType(IRigidBody::BodyType::Dynamic);

		controller->getActor()->userData = character;

		character->SetMass(params.density);
		//controller->getActor()->setMass(params.density);
		//controller->setUserData(character);

		_scene->unlockWrite();

		return character;
	}

	IRigidBody* PhysicsSystemPhysX::CreateRigidBody(Transform* transform, RigidBody* bodyComponent, IRigidBody::BodyType type, const math::Vector3& offset)
	{
		_scene->lockWrite();
		//_scene->lockRead();

		const auto& worldTM = transform->GetEntity()->GetWorldTM();

		auto position = transform->GetPosition();
		auto rotation = transform->GetRotation();		

		auto parent = transform->GetEntity()->GetParent();

		while (parent)
		{
			position += parent->GetComponent<Transform>()->GetPosition();

			rotation.RotateTowards(parent->GetComponent<Transform>()->GetRotation(), dx::g_XMTwoPi.f[0]);
			rotation.Normalize();

			parent = parent->GetParent();
		}

		//math::Quaternion::CreateFromRotationMatrix()

		physx::PxTransform localTm(*(physx::PxVec3*)&position.x, *(physx::PxQuat*)&rotation.x);

		physx::PxRigidActor* actor = nullptr;

		if (type == IRigidBody::BodyType::Static)
		{
			actor = _physics->createRigidStatic(localTm);
		}
		else
		{
			actor = _physics->createRigidDynamic(localTm);

			physx::PxRigidDynamic* dynamic = (physx::PxRigidDynamic*)actor;

			if(type == IRigidBody::BodyType::Kinematic)
				dynamic->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, true);
		}

		if (actor)
		{
			RigidBodyPhysX* rigidBody = new RigidBodyPhysX(actor, bodyComponent, transform->GetEntity());

			rigidBody->Create(transform);
			rigidBody->SetBodyType(type);

			actor->userData = rigidBody;
			_scene->addActor(*actor);

			_scene->unlockWrite();
			//_scene->unlockRead();

			return rigidBody;
		}

		_scene->unlockWrite();
		//_scene->unlockRead();

		return nullptr;
	}

	IRigidBody* PhysicsSystemPhysX::CloneRigidBody(IRigidBody* physicsBody, Transform* transform, RigidBody* bodyComponent, IRigidBody::BodyType type)
	{
		_scene->lockWrite();
		//_scene->lockRead();

		auto position = transform->GetPosition();
		auto rotation = transform->GetRotation();

		physx::PxTransform localTm(*(physx::PxVec3*)&position.x, *(physx::PxQuat*)&rotation.x);


		physx::PxRigidActor* actor = nullptr;

		if (type == IRigidBody::BodyType::Static)
		{
			actor = _physics->createRigidStatic(localTm);
		}
		else
		{
			actor = _physics->createRigidDynamic(localTm);

			physx::PxRigidDynamic* dynamic = (physx::PxRigidDynamic*)actor;

			if (type == IRigidBody::BodyType::Kinematic)
				dynamic->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, true);

		}

		if (actor)
		{
			RigidBodyPhysX* rigidBody = new RigidBodyPhysX(actor, bodyComponent, transform->GetEntity());

			rigidBody->Create(transform);
			rigidBody->SetBodyType(type);

			RigidBodyPhysX* physxBody = (RigidBodyPhysX*)physicsBody;

			// if the shape is exclusive then we must make a new shape, it cannot be copied
			if (bodyComponent->IsExclusive())
			{
				const auto scale = transform->GetScale();

				switch (physxBody->_geometry->getType())
				{
				case physx::PxGeometryType::eTRIANGLEMESH:
				{
					physx::PxTriangleMeshGeometry* triGeom = (physx::PxTriangleMeshGeometry*)physxBody->_geometry;

					physx::PxTriangleMeshGeometry* geomCopy = new physx::PxTriangleMeshGeometry(triGeom->triangleMesh, physx::PxMeshScale(*(physx::PxVec3*)&scale.x));

					rigidBody->_geometry = geomCopy;
					rigidBody->_shape = _physics->createShape(*geomCopy, *_defaultMaterial, true);
					break;
				}

				default:
					LOG_CRIT("Exclusive shape copy not implemented");
					break;
				}
				
				rigidBody->_body->attachShape(*rigidBody->_shape);
				
				//rigidBody->_shape->release();

				rigidBody->_collider = new ColliderPhysX(rigidBody->_shape, rigidBody);
			}
			else
			{

				//physxBody->_shape->acquireReference();
				rigidBody->_shape = physxBody->_shape;
				rigidBody->_body->attachShape(*physxBody->_shape);
				rigidBody->_collider = new ColliderPhysX(physxBody->_shape, rigidBody);
			}

			actor->userData = physxBody;
			_scene->addActor(*actor);

			_scene->unlockWrite();
			//_scene->unlockRead();

			return rigidBody;
		}

		_scene->unlockWrite();
		//_scene->unlockRead();

		return nullptr;
	}

	void PhysicsSystemPhysX::LockWrite()
	{
		_scene->lockWrite();
	}

	void PhysicsSystemPhysX::UnlockWrite()
	{
		_scene->unlockWrite();
	}

	void PhysicsSystemPhysX::DestroyRigidBody(IRigidBody* body)
	{
		LOG_DEBUG("Entering lock");

		_scene->lockWrite();
		//_scene->lockRead();
		{
			RigidBodyPhysX* _body = static_cast<RigidBodyPhysX*>(body);

			LOG_DEBUG("Destroying IRigidBody 0x%p", _body);

			_scene->removeActor(*(physx::PxRigidActor*)_body->GetRigidActor());

			_body->Destroy();

			delete _body;

			_scene->flushQueryUpdates();
		}
		//_scene->unlockRead();
		_scene->unlockWrite();
	}

	void PhysicsSystemPhysX::Update(float simulationTime)
	{
		if (_scene)
		{			
			_scene->lockRead();

			

			physx::PxU32 nbActiveActors = _scene->getNbActors(physx::PxActorTypeFlag::eRIGID_DYNAMIC  /*| physx::PxActorTypeFlag::eRIGID_STATIC*/);

			if (nbActiveActors > 0)
			{

				std::vector<physx::PxRigidActor*> actors(nbActiveActors);
				_scene->getActors(physx::PxActorTypeFlag::eRIGID_DYNAMIC  /*| physx::PxActorTypeFlag::eRIGID_STATIC*/, reinterpret_cast<physx::PxActor**>(&actors[0]), nbActiveActors);

				_scene->unlockRead();

				// update each render object with the new transform
				for (auto&& actor : actors)
				{
					auto body = (RigidBodyPhysX*)actor->userData;

					if (body)
					{
						
						g_pEnv->_sceneManager->GetCurrentScene()->Lock();

						auto transform = body->GetTransform();			

						auto bodyComponent = body->GetBodyComponent();

						if(bodyComponent)
							bodyComponent->EnableForcePoseUpdates(false);

						transform->SetPosition(body->GetPhysicsPosition());

						auto cct = dynamic_cast<CharacterController*>(body);

						if (cct == nullptr)
						{
							transform->SetRotation(body->GetPhysicsRotation());
						}

						if(bodyComponent)
							bodyComponent->EnableForcePoseUpdates(true);

						g_pEnv->_sceneManager->GetCurrentScene()->Unlock();
					}
				}
			}
			else
			{
				_scene->unlockRead();
			}				
		}
	}

	void PhysicsSystemPhysX::Simulate(float simulationTime)
	{
		if (_scene)
		{
			_scene->lockWrite();
			{
				_scene->simulate(simulationTime);	
				_scene->fetchResults(true);

				if (_waitingForSimulationStepForDebug)
					_didDoDebugSimulationStep = true;
			}
			_scene->unlockWrite();
		}
	}

	void PhysicsSystemPhysX::DebugRender()
	{
#ifdef _DEBUG
		if (_resetDebug)
		{
			if (_waitingForSimulationStepForDebug == false)
			{
				_scene->lockWrite();
				_scene->setVisualizationParameter(physx::PxVisualizationParameter::eSCALE, 1.0f);
				_scene->setVisualizationParameter(physx::PxVisualizationParameter::eCOLLISION_STATIC, 1.0f);
				_scene->setVisualizationParameter(physx::PxVisualizationParameter::eCOLLISION_DYNAMIC, 1.0f);
				//_scene->setVisualizationParameter(physx::PxVisualizationParameter::eCOLLISION_AABBS, 1.0f);
				_scene->setVisualizationParameter(physx::PxVisualizationParameter::eCOLLISION_SHAPES, 1.0f);

				_waitingForSimulationStepForDebug = true;
				_scene->unlockWrite();
			}
			else if (_didDoDebugSimulationStep)
			{
				_scene->lockRead();
				{
					const physx::PxRenderBuffer& rb = _scene->getRenderBuffer();
					_debugLines.clear();
					_debugLines.insert(_debugLines.end(), (physx::PxDebugLine*)rb.getLines(), (physx::PxDebugLine*)rb.getLines() + rb.getNbLines());

					_debugTriangles.clear();
					_debugTriangles.insert(_debugTriangles.end(), (physx::PxDebugTriangle*)rb.getTriangles(), (physx::PxDebugTriangle*)rb.getTriangles() + (rb.getNbTriangles()));
				}
				_scene->unlockRead();

				_scene->lockWrite();
				_scene->setVisualizationParameter(physx::PxVisualizationParameter::eSCALE, 0.0f);
				_scene->setVisualizationParameter(physx::PxVisualizationParameter::eCOLLISION_STATIC, 0.0f);
				_scene->setVisualizationParameter(physx::PxVisualizationParameter::eCOLLISION_DYNAMIC, 0.0f);
				//_scene->setVisualizationParameter(physx::PxVisualizationParameter::eCOLLISION_AABBS, 1.0f);
				_scene->setVisualizationParameter(physx::PxVisualizationParameter::eCOLLISION_SHAPES, 0.0f);
				_scene->unlockWrite();

				_didDoDebugSimulationStep = false;
				_resetDebug = false;
				_waitingForSimulationStepForDebug = false;
			}
		}

		for(auto& line : _debugLines)
		{
			g_pEnv->_debugRenderer->DrawLine(
				*(math::Vector3*)&line.pos0.x,
				*(math::Vector3*)&line.pos1.x,
				math::Color(HEX_RGBA_TO_FLOAT4(239, 9, 242, 255)));
			// render the line
		}

		for(auto& triangle : _debugTriangles)
		{
			g_pEnv->_debugRenderer->DrawLine(
				*(math::Vector3*)&triangle.pos0.x,
				*(math::Vector3*)&triangle.pos1.x,
				math::Color(HEX_RGBA_TO_FLOAT4(3, 242, 237, 255)));

			g_pEnv->_debugRenderer->DrawLine(
				*(math::Vector3*)&triangle.pos1.x,
				*(math::Vector3*)&triangle.pos2.x,
				math::Color(HEX_RGBA_TO_FLOAT4(3, 242, 237, 255)));

			g_pEnv->_debugRenderer->DrawLine(
				*(math::Vector3*)&triangle.pos2.x,
				*(math::Vector3*)&triangle.pos0.x,
				math::Color(HEX_RGBA_TO_FLOAT4(3, 242, 237, 255)));
			// render the line
		}	
#endif
	}

	physx::PxPhysics* PhysicsSystemPhysX::GetPhysics()
	{
		return _physics;
	}

	physx::PxMaterial* PhysicsSystemPhysX::GetDefaultMaterial()
	{
		return _defaultMaterial;
	}

	physx::PxCooking* PhysicsSystemPhysX::GetCooking()
	{
		return _cooking;
	}

	physx::PxScene* PhysicsSystemPhysX::GetScene()
	{
		return _scene;
	}

	PX_INLINE void addForceAtPosInternal(physx::PxRigidBody& body, const physx::PxVec3& force, const physx::PxVec3& pos, physx::PxForceMode::Enum mode, bool wakeup)
	{
		/*	if(mode == PxForceMode::eACCELERATION || mode == PxForceMode::eVELOCITY_CHANGE)
			{
				Ps::getFoundation().error(PxErrorCode::eINVALID_PARAMETER, __FILE__, __LINE__,
					"PxRigidBodyExt::addForce methods do not support eACCELERATION or eVELOCITY_CHANGE modes");
				return;
			}*/

		const physx::PxTransform globalPose = body.getGlobalPose();
		const physx::PxVec3 centerOfMass = globalPose.transform(body.getCMassLocalPose().p);

		const physx::PxVec3 torque = (pos - centerOfMass).cross(force);
		body.addForce(force, mode, wakeup);
		body.addTorque(torque, mode, wakeup);
	}

	static void addForceAtLocalPos(physx::PxRigidBody& body, const physx::PxVec3& force, const physx::PxVec3& pos, physx::PxForceMode::Enum mode, bool wakeup = true)
	{
		//transform pos to world space
		const physx::PxVec3 globalForcePos = body.getGlobalPose().transform(pos);

		addForceAtPosInternal(body, force, globalForcePos, mode, wakeup);
	}

	void PhysicsSystemPhysX::onShapeHit(const physx::PxControllerShapeHit& hit)
	{
		physx::PxRigidActor* _actor = hit.actor;// shape->getActor();
		
		if (!_actor)
			return;
		
		physx::PxRigidDynamic* actor = _actor->is<physx::PxRigidDynamic>();

		if (actor)
		{
			if (actor->getRigidBodyFlags() & physx::PxRigidBodyFlag::eKINEMATIC)
				return;

			//if (0)
			//{
			//	const PxVec3 p = actor->getGlobalPose().p + hit.dir * 10.0f;

			//	PxShape* shape;
			//	actor->getShapes(&shape, 1);
			//	PxRaycastHit newHit;
			//	PxU32 n = PxShapeExt::raycast(*shape, *shape->getActor(), p, -hit.dir, 20.0f, PxHitFlag::ePOSITION, 1, &newHit);
			//	if (n)
			//	{
			//		// We only allow horizontal pushes. Vertical pushes when we stand on dynamic objects creates
			//		// useless stress on the solver. It would be possible to enable/disable vertical pushes on
			//		// particular objects, if the gameplay requires it.
			//		const PxVec3 upVector = hit.controller->getUpDirection();
			//		const PxF32 dp = hit.dir.dot(upVector);
			//		//		shdfnd::printFormatted("%f\n", fabsf(dp));
			//		if (fabsf(dp) < 1e-3f)
			//			//		if(hit.dir.y==0.0f)
			//		{
			//			const PxTransform globalPose = actor->getGlobalPose();
			//			const PxVec3 localPos = globalPose.transformInv(newHit.position);
			//			::addForceAtLocalPos(*actor, hit.dir * hit.length * 1000.0f, localPos, PxForceMode::eACCELERATION);
			//		}
			//	}
			//}

			// We only allow horizontal pushes. Vertical pushes when we stand on dynamic objects creates
			// useless stress on the solver. It would be possible to enable/disable vertical pushes on
			// particular objects, if the gameplay requires it.
			const physx::PxVec3 upVector = hit.controller->getUpDirection();
			const physx::PxF32 dp = hit.dir.dot(upVector);
			//		shdfnd::printFormatted("%f\n", fabsf(dp));
			if (fabsf(dp) < 1e-3f)
				//		if(hit.dir.y==0.0f)
			{
				const physx::PxTransform globalPose = actor->getGlobalPose();
				const physx::PxVec3 localPos = globalPose.transformInv(toVec3(hit.worldPos));

				addForceAtLocalPos(*actor, hit.dir * hit.length * 10.0f, localPos, physx::PxForceMode::eACCELERATION);
			}
		}
	}

	void PhysicsSystemPhysX::onTrigger(physx::PxTriggerPair* pairs, physx::PxU32 count)
	{
		for (physx::PxU32 i = 0; i < count; i++)
		{
			const physx::PxTriggerPair& cp = pairs[i];

			// ignore pairs when shapes have been deleted
			if (pairs[i].flags & (physx::PxTriggerPairFlag::eREMOVED_SHAPE_TRIGGER |
				physx::PxTriggerPairFlag::eREMOVED_SHAPE_OTHER))
				continue;

			{
				RigidBodyPhysX* triggerBody = (RigidBodyPhysX*)cp.triggerActor->userData;
				RigidBodyPhysX* actorBody = (RigidBodyPhysX*)cp.otherActor->userData;

				Entity* triggerEnt = (Entity*)triggerBody->GetEntity();
				Entity * actorEnt = (Entity*)actorBody->GetEntity();

				if (triggerEnt && actorEnt)
				{
					if (cp.status == physx::PxPairFlag::eNOTIFY_TOUCH_LOST)
					{
						LeaveTriggerMessage message;
						message.trigger = triggerEnt;
						actorEnt->OnMessage(&message, triggerEnt);
					}
					else if (cp.status == physx::PxPairFlag::eNOTIFY_TOUCH_FOUND)
					{
						EnterTriggerMessage message;
						message.trigger = triggerEnt;
						actorEnt->OnMessage(&message, triggerEnt);
					}
				}
			}
		}
	}

	uint32_t PhysicsSystemPhysX::RayCast(const math::Vector3& from, const math::Vector3& unitDir, float maxDist, IRigidBody* body, RayHit* hitInfo)
	{
		physx::PxVec3 _from(from.x, from.y, from.z);
		physx::PxVec3 _unitDir(unitDir.x, unitDir.y, unitDir.z);
		RigidBodyPhysX* bodyPhysx = (RigidBodyPhysX*)body;
		physx::PxHitFlags hitFlags = physx::PxHitFlag::ePOSITION | physx::PxHitFlag::eNORMAL | physx::PxHitFlag::eUV | physx::PxHitFlag::eMESH_ANY;

		physx::PxQueryFilterData fd;
		//fd.flags |= physx::PxQueryFlag::eANY_HIT; // note the OR with the default value

		physx::PxRaycastHit hit;

		if (!bodyPhysx)
			return 0;

		auto bodyComp = bodyPhysx->GetBodyComponent();

		if (!bodyComp)
			return 0;

		Entity* entity = bodyComp->GetEntity();

		if (bodyPhysx->_shape == nullptr || bodyPhysx->_body == nullptr)
			return 0;

		_scene->lockRead();
		physx::PxTransform pose = physx::PxShapeExt::getGlobalPose(*bodyPhysx->_shape, *bodyPhysx->_body);
		_scene->unlockRead();
		//pose.p = 

		uint32_t numHits = physx::PxGeometryQuery::raycast(
			_from,
			_unitDir,
			*bodyPhysx->_geometry,
			pose,
			maxDist,
			hitFlags,
			1,
			&hit);

		if (numHits > 0)
		{
			hitInfo->position = math::Vector3(hit.position.x, hit.position.y, hit.position.z);
			hitInfo->distance = hit.distance;
			hitInfo->entity = entity;
			hitInfo->normal = math::Vector3(hit.normal.x, hit.normal.y, hit.normal.z);
			hitInfo->start = from;
		}

		return numHits;
	}

	void PhysicsSystemPhysX::onControllerHit(const physx::PxControllersHit& hit)
	{

	}
	
	void PhysicsSystemPhysX::onObstacleHit(const physx::PxControllerObstacleHit& hit)
	{

	}

	void PhysicsSystemPhysX::onContact(const physx::PxContactPairHeader& pairHeader, const physx::PxContactPair* pairs, physx::PxU32 nbPairs)
	{
		//_scene->lockRead();

		for (physx::PxU32 i = 0; i < nbPairs; i++)
		{
			const physx::PxContactPair& cp = pairs[i];

			if (cp.events & physx::PxPairFlag::eNOTIFY_TOUCH_FOUND)
			{
				
				RigidBodyPhysX* px0 = (RigidBodyPhysX*)pairHeader.actors[0]->userData;
				RigidBodyPhysX* px1 = (RigidBodyPhysX*)pairHeader.actors[1]->userData;

				if (px0 && px1 && px0->GetBodyComponent() && px1->GetBodyComponent())
				{
					math::Vector3* points = (math::Vector3*)cp.contactPoints;

					math::Vector3 p0, p1;

					if (!points)
					{
						p0 = px0->GetPhysicsPosition();
						p1 = px1->GetPhysicsPosition();
					}
					else
					{
						p0 = points[0];
						p1 = points[1];
					}

					RigidBodyCollision message;
					message.collidedWith = px1->GetEntity();
					message.collisionPoint = p0;
					px0->GetEntity()->OnMessage(&message, nullptr);

					RigidBodyCollision message2;
					message2.collidedWith = px0->GetEntity();
					message2.collisionPoint = p1;
					px1->GetEntity()->OnMessage(&message2, nullptr);

					//px0->GetBodyComponent()->OnCollision(px1->GetBodyComponent(), p0);
					//px1->GetBodyComponent()->OnCollision(px0->GetBodyComponent(), p1);
				}

				//if ((pairHeader.actors[0] == mSubmarineActor) ||
				//	(pairHeader.actors[1] == mSubmarineActor))
				//{
				//	PxActor* otherActor = (mSubmarineActor == pairHeader.actors[0]) ?
				//		pairHeader.actors[1] : pairHeader.actors[0];
				//	Seamine* mine = reinterpret_cast<Seamine*>(otherActor->userData);
				//	// insert only once
				//	if (std::find(mMinesToExplode.begin(), mMinesToExplode.end(), mine) ==
				//		mMinesToExplode.end())
				//		mMinesToExplode.push_back(mine);

				//	break;
				//}
			}
		}

		//_scene->unlockRead();
	}

	void PhysicsSystemPhysX::CreateHingeJoint(IRigidBody* bodies[2], const math::Vector3 axes[2], const math::Vector3 offsets[2])
	{
		RigidBodyPhysX* p1 = (RigidBodyPhysX*)bodies[0];
		RigidBodyPhysX* p2 = (RigidBodyPhysX*)bodies[1];

		physx::PxMat44 mat1(physx::PxIDENTITY::PxIdentity);
		physx::PxMat44 mat2(physx::PxIDENTITY::PxIdentity);

		mat1.column0.x = axes[0].x; mat1.column0.y = axes[0].y; mat1.column0.z = axes[0].z;
		mat2.column0.x = axes[1].x; mat2.column0.y = axes[1].y; mat2.column0.z = axes[1].z;

		mat1.setPosition(physx::PxVec3(offsets[0].x, offsets[0].y, offsets[0].z));
		mat2.setPosition(physx::PxVec3(offsets[1].x, offsets[1].y, offsets[1].z));
		
		//physx::PxTransform transform1(physx::PxVec3(offsets[0].x, offsets[0].y, offsets[0].z));
		//physx::PxTransform transform2(physx::PxVec3(offsets[1].x, offsets[1].y, offsets[1].z));

		physx::PxTransform transform1(mat1);
		physx::PxTransform transform2(physx::PxIDENTITY::PxIdentity/*mat2*/);

		transform1.q.normalize();
		transform2.q.normalize();

		_scene->lockWrite();

		physx::PxRevoluteJoint* joint = physx::PxRevoluteJointCreate(
			*_physics,
			p1 ? p1->GetRigidActor() : nullptr,
			transform1,
			p2 ? p2->GetRigidActor() : nullptr,
			transform2);

		_scene->unlockWrite();

		//joint->set
	}
}