

#include "RigidBody.hpp"
#include "../../HexEngine.hpp"

#include <algorithm>

namespace HexEngine
{
	namespace
	{
		dx::BoundingBox GetScaledEntityAABB(Entity* entity)
		{
			dx::BoundingBox scaled = entity->GetAABB();
			scaled.Transform(scaled, math::Matrix::CreateScale(entity->GetAbsoluteScale()));
			return scaled;
		}
	}

	RigidBody::RigidBody(Entity* entity, IRigidBody::BodyType bodyType) :
		BaseComponent(entity),
		_bodyType(bodyType)
	{
		
	}

	//RigidBody::RigidBody(Entity* entity, IRigidBody::BodyType bodyType, const math::Vector3& bodyOffset) :
	//	BaseComponent(entity),
	//	_bodyType(bodyType)
	//{
	//	// Create the rigid body
	//	//
	//	_rigidBody = g_pEnv->_physicsSystem->CreateRigidBody(GetEntity()->GetComponent<Transform>(), this, bodyType, bodyOffset);
	//}

	RigidBody::RigidBody(Entity* entity, RigidBody* copy) : 
		BaseComponent(entity)
	{
		_exclusive = copy->_exclusive;
		_colliderShape = copy->_colliderShape;
		_colliderData = copy->_colliderData;
		// Copy the body-config flags too - a clone that dropped these ended up
		// with default gravity/trigger state (a spawned prefab body that wasn't
		// gravity-driven until manually re-toggled).
		_isGravityApplied = copy->_isGravityApplied;
		_isTrigger = copy->_isTrigger;
		_massAdjust = copy->_massAdjust;

		if (auto rb = copy->GetIRigidBody())
		{
			// The actor's live type is the source of truth (the member _bodyType
			// isn't reliably set on the source; Serialize uses GetBodyType()).
			_bodyType = rb->GetBodyType();
			_rigidBody = g_pEnv->_physicsSystem->CloneRigidBody(
				rb,
				GetEntity()->GetComponent<Transform>(),
				this,
				_bodyType);

			if (_rigidBody != nullptr)
			{
				_rigidBody->SetGravityEnabled(_isGravityApplied);
				if (_isTrigger)
					_rigidBody->SetIsTrigger(true);
			}
		}
	}

	void RigidBody::Destroy()
	{
		LOG_DEBUG("Destroring rigid body");

		if (_rigidBody)
		{
			g_pEnv->_physicsSystem->DestroyRigidBody(_rigidBody);
			_rigidBody = nullptr;
		}
	}

	//void RigidBody::FixedUpdate(float frameTime)
	//{
	//	return;

	//	if (_rigidBody->GetBodyType() == IRigidBody::BodyType::Dynamic || _rigidBody->GetBodyType() == IRigidBody::BodyType::Kinematic)
	//	{
	//		// we need to update the transform because physics will have been calculated already by this point
	//		//
	//		auto transform = GetEntity()->GetComponent<Transform>();

	//		transform->SetPosition(_rigidBody->GetPhysicsPosition());
	//		transform->SetRotation(math::Quaternion::CreateFromRotationMatrix(_rigidBody->GetPhysicsRotation()));
	//		//transform->SetRotation(_rigidBody->GetPhysicsRotation());

	//		//LOG_DEBUG("Entity '%s' new position from physics is %.2f %.2f %.2f", GetEntity()->GetName().c_str(), physicsPos.x, physicsPos.y, physicsPos.z);
	//	}
	//}

	void RigidBody::AddSphereCollider(float radius)
	{
		if(!_rigidBody)
			_rigidBody = g_pEnv->_physicsSystem->CreateRigidBody(GetEntity()->GetComponent<Transform>(), this, _bodyType);

		_colliderData.sphere.radius = radius;
		_colliderShape = IRigidBody::ColliderShape::Sphere;
		_exclusive = true;

		_rigidBody->AddSphereCollider(GetEntity()->GetComponent<Transform>(), radius);
	}

	void RigidBody::AddCapsuleCollider(float radius, float height, int axis)
	{
		if (!_rigidBody)
			_rigidBody = g_pEnv->_physicsSystem->CreateRigidBody(GetEntity()->GetComponent<Transform>(), this, _bodyType);

		_colliderData.capsule.radius = radius;
		_colliderData.capsule.height = height;
		_colliderData.capsule.axis = axis;
		_colliderShape = IRigidBody::ColliderShape::Capsule;

		// Exclusive (like Box): a per-instance shape, so cloning a prefab gives
		// each copy its own capsule (with its own axis pose and material) rather
		// than sharing one - the drivable chassis mutates its material live.
		_exclusive = true;

		_rigidBody->AddCapsuleCollider(GetEntity()->GetComponent<Transform>(), radius, height, axis);
	}

#if 0
	void RigidBody::AddTerrainCollider(Terrain* terrain)
	{
		//_colliderData.sphere.radius = radius;
		_colliderShape = IRigidBody::ColliderShape::HeightField;

		auto numSquares = terrain->GetNumberOfTiles() + 1;

		// Create the heightfield array
		//
		float* heightValues = new float[numSquares * numSquares];

		float width = terrain->GetWidth();

		float lowest = FLT_MAX;
		float highest = -FLT_MAX;

		for (uint32_t i = 0; i < numSquares; ++i)
		{
			for (uint32_t j = 0; j < numSquares; ++j)
			{
				float tileSize = terrain->GetTileWidth();

				if (terrain->HasHeightMap())
				{
					uint32_t x = j;// (numSquares - j) - 1;
					uint32_t y = i;// (numSquares - i) - 1;

					float heightVal = terrain->GetHeightMap().at(i * numSquares + j);

					//heightVal += 32.0f;

					if (heightVal > highest)
						highest = heightVal;

					if (heightVal < lowest)
						lowest = heightVal;

					heightValues[i * numSquares + j] = heightVal;

					
				}
				else
					heightValues[i * numSquares + j] = 0.0f;

				
			}
		}

		auto aabb = terrain->GetAABB();

		float minHeight = lowest;// -aabb.Extents.y / 2.0f;
		float maxHeight = highest;// -minHeight;

		minHeight -= aabb.Center.y;
		maxHeight -= aabb.Center.y;

		//minHeight += 32.0f;
		//maxHeight += 32.0f;

		//float minHeight = -aabb.Extents.y;
		//float maxHeight = -minHeight;

		//minHeight -= aabb.Center.y;
		//maxHeight -= aabb.Center.y;

		LOG_DEBUG("Creating height field collider with min and max of %f - %.3f. [AABB = (%f %f %f) -- (%f %f %f)]",
			lowest, highest, 
			aabb.Extents.x, aabb.Extents.y, aabb.Extents.z,
			aabb.Center.x, aabb.Center.y, aabb.Center.z);

		float totalSize = terrain->GetTileWidth() * (float)numSquares;

		math::Vector3 physicsPosition = terrain->GetPosition();// -math::Vector3((totalSize / 4.0f), 0.0f, (totalSize / 4.0f));

		_rigidBody->AddHeightFieldCollider(
			numSquares,
			numSquares,
			minHeight,
			maxHeight,
			heightValues,
			physicsPosition,
			terrain->GetTileWidth());

		//SAFE_DELETE_ARRAY(heightValues);
	}
#endif

	void RigidBody::AddBoxCollider(const dx::BoundingBox& box)
	{
		math::Vector3 extents = box.Extents;

		if (extents.Length() <= 0.0f)
			return;

		if (!_rigidBody)
			_rigidBody = g_pEnv->_physicsSystem->CreateRigidBody(GetEntity()->GetComponent<Transform>(), this, _bodyType);

		_colliderData.box.aabb = box;
		_colliderShape = IRigidBody::ColliderShape::Box;

		_rigidBody->AddBoxCollider(box, true);

		_exclusive = true;
	}

	void RigidBody::AddTriangleMeshCollider(const std::vector<math::Vector3>& vertices, const std::vector<MeshIndexFormat>& indices, uint32_t faceCount, bool exclusive)
	{
		if (!_rigidBody)
			_rigidBody = g_pEnv->_physicsSystem->CreateRigidBody(GetEntity()->GetComponent<Transform>(), this, _bodyType);

		IRigidBody::ColliderData::Mesh mesh;

		mesh.vertices = vertices;
		mesh.indices = indices;
		mesh.faceCount = faceCount;

		_colliderData.meshes.push_back(mesh);

		_colliderShape = IRigidBody::ColliderShape::TriangleMesh;

		_exclusive = exclusive;

		_rigidBody->AddTriangleMeshCollider(vertices, indices, faceCount, exclusive);
	}

	void RigidBody::AddConvexMeshCollider(const std::vector<math::Vector3>& vertices, const std::vector<MeshIndexFormat>& indices, uint32_t faceCount, bool exclusive)
	{
		if (!_rigidBody)
			_rigidBody = g_pEnv->_physicsSystem->CreateRigidBody(GetEntity()->GetComponent<Transform>(), this, _bodyType);

		IRigidBody::ColliderData::Mesh mesh;

		mesh.vertices = vertices;
		mesh.indices = indices;
		mesh.faceCount = faceCount;

		_colliderData.meshes.push_back(mesh);

		_colliderShape = IRigidBody::ColliderShape::TriangleMesh;

		_exclusive = exclusive;

		_rigidBody->AddConvexMeshCollider(vertices, indices, faceCount, exclusive);
	}

	void RigidBody::AddTriangleMeshCollider(Mesh* meshIn, bool exclusive)
	{
		if (!_rigidBody)
			_rigidBody = g_pEnv->_physicsSystem->CreateRigidBody(GetEntity()->GetComponent<Transform>(), this, _bodyType);

		IRigidBody::ColliderData::Mesh mesh;

		for (auto& verts : meshIn->GetVertices())
			mesh.vertices.push_back(math::Vector3(verts._position));

		mesh.indices = meshIn->GetIndices();
		mesh.faceCount = meshIn->GetNumFaces();

		_colliderData.meshes.push_back(mesh);

		_colliderShape = IRigidBody::ColliderShape::TriangleMesh;

		_exclusive = exclusive;

		_rigidBody->AddTriangleMeshCollider(mesh.vertices, mesh.indices, mesh.faceCount, exclusive);
	}

	bool RigidBody::BeginAddTriangleMeshColliderAsync(const std::vector<math::Vector3>& vertices, const std::vector<MeshIndexFormat>& indices, uint32_t faceCount, bool exclusive)
	{
		if (!_rigidBody)
			_rigidBody = g_pEnv->_physicsSystem->CreateRigidBody(GetEntity()->GetComponent<Transform>(), this, _bodyType);

		// Mirror the bookkeeping the sync version does so that downstream
		// state queries (collider shape kind, exclusive flag, stored mesh
		// data for serialisation / debug) match either path.
		IRigidBody::ColliderData::Mesh mesh;
		mesh.vertices = vertices;
		mesh.indices = indices;
		mesh.faceCount = faceCount;
		_colliderData.meshes.push_back(mesh);
		_colliderShape = IRigidBody::ColliderShape::TriangleMesh;
		_exclusive = exclusive;

		return _rigidBody->BeginAddTriangleMeshColliderAsync(vertices, indices, faceCount, exclusive);
	}

	bool RigidBody::BeginAddTriangleMeshColliderAsync(Mesh* meshIn, bool exclusive)
	{
		std::vector<math::Vector3> verts;
		verts.reserve(meshIn->GetVertices().size());
		for (auto& v : meshIn->GetVertices())
			verts.push_back(math::Vector3(v._position));
		return BeginAddTriangleMeshColliderAsync(verts, meshIn->GetIndices(), meshIn->GetNumFaces(), exclusive);
	}

	bool RigidBody::TryFinishAsyncCollider()
	{
		return _rigidBody != nullptr && _rigidBody->TryFinishAsyncCollider();
	}

	bool RigidBody::HasAsyncColliderInFlight() const
	{
		return _rigidBody != nullptr && _rigidBody->HasAsyncColliderInFlight();
	}

	void RigidBody::AddTriangleMeshColliderFromCookedBuffer(const std::vector<uint8_t>& cookedBuffer, bool exclusive)
	{
		if (!_rigidBody)
			_rigidBody = g_pEnv->_physicsSystem->CreateRigidBody(GetEntity()->GetComponent<Transform>(), this, _bodyType);

		_colliderShape = IRigidBody::ColliderShape::TriangleMesh;
		_exclusive = exclusive;

		_rigidBody->AddTriangleMeshColliderFromCookedBuffer(cookedBuffer, exclusive);
	}

	const std::vector<uint8_t>& RigidBody::GetLastCookedBuffer() const
	{
		static const std::vector<uint8_t> empty;
		return _rigidBody != nullptr ? _rigidBody->GetLastCookedBuffer() : empty;
	}

	void RigidBody::AddConvexMeshCollider(Mesh* meshIn, bool exclusive)
	{
		if (!_rigidBody)
			_rigidBody = g_pEnv->_physicsSystem->CreateRigidBody(GetEntity()->GetComponent<Transform>(), this, _bodyType);

		IRigidBody::ColliderData::Mesh mesh;

		for (auto& verts : meshIn->GetVertices())
			mesh.vertices.push_back(math::Vector3(verts._position));

		mesh.indices = meshIn->GetIndices();
		mesh.faceCount = meshIn->GetNumFaces();

		_colliderData.meshes.push_back(mesh);

		_colliderShape = IRigidBody::ColliderShape::TriangleMesh;

		_exclusive = exclusive;

		_rigidBody->AddConvexMeshCollider(mesh.vertices, mesh.indices, mesh.faceCount, exclusive);
	}

	void RigidBody::OnMessage(Message* message, MessageListener* sender)
	{
		MessageListener::OnMessage(message, sender);

		if (message->_id == MessageId::TransformChanged)
		{
			auto transformChanged = message->CastAs<TransformChangedMessage>();

			if ((transformChanged->_flags & TransformChangedMessage::ChangeFlags::ScaleChanged) != 0)
			{
				if (_colliderShape == IRigidBody::ColliderShape::Box)
				{
					auto scaledAabb = GetScaledEntityAABB(GetEntity());
					GetIRigidBody()->UpdateBoxExtents(scaledAabb);
					_colliderData.box.aabb = scaledAabb;
				}
				else if (_colliderShape == IRigidBody::ColliderShape::TriangleMesh)
				{
					GetIRigidBody()->UpdateTriangleMeshScale(transformChanged->_scale);
				}
			}
			
			if ((transformChanged->_flags & TransformChangedMessage::ChangeFlags::PositionChanged) != 0)
			{
				if(_forcePoseEnabled)
					ForceUpdatePose();
			}

			if ((transformChanged->_flags & TransformChangedMessage::ChangeFlags::RotationChanged) != 0)
			{
				if(_forcePoseEnabled)
					ForceUpdatePose();
			}
		}
	}

	void RigidBody::EnableForcePoseUpdates(bool enable)
	{
		_forcePoseEnabled = enable;
	}

	void RigidBody::ForceUpdatePose()
	{
		auto rb = GetIRigidBody();

		if (rb == nullptr)
			return;

		// A character controller owns its own position (Move() + the physics
		// read-back). Letting transform-change messages force-pose it fights the
		// controller and, because each setFootPosition runs overlap recovery
		// against world geometry, tanks the framerate when fired per-frame by
		// mouse-look rotation. Explicit spawn/respawn teleports call
		// UpdatePosePosition directly, bypassing this path.
		if (rb->IsCharacterController())
			return;

		if (rb->GetBodyType() != IRigidBody::BodyType::Static)
		{
			auto transform = GetEntity()->GetComponent<Transform>();

			const auto worldTM = GetEntity()->GetWorldTM();
			const auto worldPosition = worldTM.Translation();

			math::Quaternion worldRotation = transform->GetRotation();
			for (auto* parent = GetEntity()->GetParent(); parent != nullptr; parent = parent->GetParent())
			{
				worldRotation = worldRotation * parent->GetRotation();
				worldRotation.Normalize();
			}

			GetIRigidBody()->UpdatePosePosition(worldPosition);
			GetIRigidBody()->UpdatePoseRotation(worldRotation);
		}
		else if (rb->GetBodyType() == IRigidBody::BodyType::Static)
		{
			//auto transform = GetEntity()->GetComponent<Transform>();
			GetEntity()->ForcePosition(GetEntity()->GetPosition());
			GetEntity()->ForceRotation(GetEntity()->GetRotation());
			/*auto transform = GetEntity()->GetComponent<Transform>();

			auto position = transform->GetPosition();
			auto rotation = transform->GetRotation();

			auto parent = transform->GetEntity()->GetParent();

			while (parent)
			{
				position += parent->GetComponent<Transform>()->GetPosition();

				rotation.RotateTowards(parent->GetComponent<Transform>()->GetRotation(), dx::g_XMTwoPi.f[0]);
				rotation.Normalize();

				parent = parent->GetParent();
			}*/

			//rotation.RotateTowards(parent->GetComponent<Transform>()->GetRotation(), dx::g_XMTwoPi.f[0]);
			//rotation.Normalize();

			//rb->UpdatePosePosition(position);

			//GetIRigidBody()->UpdatePoseRotation(rotation);
		}
	}
	//void RigidBody::OnCollision(RigidBody* body, const math::Vector3& point)
	//{
	//	//g_pEnv->_audioManager->Play(_collisionSound, point);
	//}

	//void RigidBody::OnTrigger(RigidBody* trigger)
	//{
	//	bool a = false;
	//}

	void RigidBody::Serialize(json& data, JsonFile* file)
	{
		IRigidBody::BodyType type = _rigidBody ? _rigidBody->GetBodyType() : IRigidBody::BodyType::Static;
		float mass = _rigidBody ? _rigidBody->GetMass() : 1.0f;
		IRigidBody::ColliderShape shape = _colliderShape;

		file->Serialize(data, "_bodyType", type);
		file->Serialize(data, "_mass", mass);
		file->Serialize(data, "_shape", shape);		
		file->Serialize(data, "_exclusive", _exclusive);
		file->Serialize(data, "_isTrigger", _isTrigger);
		file->Serialize(data, "_isGravityApplied", _isGravityApplied);

		json& shapeData = data["shapeData"];

		switch (shape)
		{
			case IRigidBody::ColliderShape::Box:
				file->Serialize(shapeData, "box", _colliderData.box);
				break;

			case IRigidBody::ColliderShape::Sphere:
				file->Serialize(shapeData, "sphere", _colliderData.sphere);
				break;

			case IRigidBody::ColliderShape::Capsule:
				file->Serialize(shapeData, "capsule", _colliderData.capsule);
				break;

			case IRigidBody::ColliderShape::HeightField:
				break;

			case IRigidBody::ColliderShape::TriangleMesh:
			{
				/*auto numMeshes = _colliderData.meshes.size();
				file->Write(&numMeshes, sizeof(uint32_t));

				for (auto& mesh : _colliderData.meshes)
				{
					auto numVerts = (uint32_t)mesh.vertices.size();
					auto numInds = (uint32_t)mesh.indices.size();
					file->Write(&numVerts, sizeof(uint32_t));
					file->Write(&numInds, sizeof(uint32_t));
					file->Write(&mesh.faceCount, sizeof(uint32_t));
					file->Write(mesh.vertices.data(), sizeof(math::Vector3) * numVerts);
					file->Write(mesh.indices.data(), sizeof(uint32_t) * numInds);
					file->Write(&_exclusive, sizeof(bool));
				}*/
				break;
			}
		}
	}

	void RigidBody::Deserialize(json& data, JsonFile* file, uint32_t mask)
	{
		IRigidBody::BodyType type;
		float mass;

		RemoveCollider();

		file->Deserialize(data, "_bodyType", type);
		file->Deserialize(data, "_mass", mass);
		file->Deserialize(data, "_shape", _colliderShape);
		file->Deserialize(data, "_exclusive", _exclusive);
		file->Deserialize(data, "_isTrigger", _isTrigger);
		file->Deserialize(data, "_isGravityApplied", _isGravityApplied);

		json& shapeData = data["shapeData"];		

		switch (_colliderShape)
		{
			case IRigidBody::ColliderShape::Box:
				file->Deserialize(shapeData, "box", _colliderData.box);
				AddBoxCollider(_colliderData.box.aabb);
				break;

			case IRigidBody::ColliderShape::Sphere:
				file->Deserialize(shapeData, "sphere", _colliderData.sphere);
				AddSphereCollider(_colliderData.sphere.radius);
				break;

			case IRigidBody::ColliderShape::Capsule:
				file->Deserialize(shapeData, "capsule", _colliderData.capsule);
				AddCapsuleCollider(_colliderData.capsule.radius, _colliderData.capsule.height, _colliderData.capsule.axis);
				break;

			//case IRigidBody::ColliderShape::HeightField:
			//	AddTer
			//	assert(0); // todo
			//	file->Read(&_colliderData.terrain, sizeof(ColliderData::Terrain));
			//	break;

			case IRigidBody::ColliderShape::TriangleMesh:
			{
				if(auto mesh = GetEntity()->GetComponent<HexEngine::StaticMeshComponent>()->GetMesh(); mesh != nullptr)
				{
					AddTriangleMeshCollider(mesh.get(), true);
				}
				else
				{
					LOG_WARN("'%s' is trying to add a triangle mesh collider with an invalid StaticMeshComponent", GetEntity()->GetName().c_str());
				}
				break;
			}
		}

		if (_rigidBody)
		{
			_rigidBody->SetBodyType(type);
			_rigidBody->SetMass(mass);
			_rigidBody->SetGravityEnabled(_isGravityApplied);
		}

		// Keep the component members in step with what we just applied, so a
		// later Serialize / clone of this component reflects the real state.
		_bodyType = type;
		_massAdjust = mass;

		if (_isTrigger)
		{
			if(_rigidBody)
				_rigidBody->SetIsTrigger(_isTrigger);

			GetEntity()->SetLayer(Layer::Trigger);
		}

		//ForceUpdatePose();
		
	}

	void RigidBody::RemoveCollider()
	{
		if(_rigidBody)
			_rigidBody->RemoveCollider();

		_colliderShape = IRigidBody::ColliderShape::None;
	}

	IRigidBody::ColliderShape RigidBody::GetColliderShape() const
	{
		return _colliderShape;
	}

	bool RigidBody::IsExclusive() const
	{
		return _exclusive;
	}

	bool RigidBody::CreateWidget(ComponentWidget* widget)
	{
		DropDown* bodyType = new DropDown(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Type");
		bodyType->SetPrefabOverrideBinding(GetComponentName(), "/_bodyType");
		bodyType->SetLabelMinSize(130);
		bodyType->GetContextMenu()->Disable();

		auto rb = GetIRigidBody();

		if (rb)
		{
			switch (rb->GetBodyType())
			{
			case IRigidBody::BodyType::Static: bodyType->SetValue(L"Static"); break;
			case IRigidBody::BodyType::Dynamic: bodyType->SetValue(L"Dynamic"); break;
			case IRigidBody::BodyType::Kinematic: bodyType->SetValue(L"Kinematic"); break;

			case IRigidBody::BodyType::None:
			default:
				bodyType->SetValue(L"None"); break;
			}
		}
		else
			bodyType->SetValue(L"None");
		

		bodyType->GetContextMenu()->AddItem(new ContextItem(L"Static", std::bind(&RigidBody::SetBodyTypeFromWidget, this, IRigidBody::BodyType::Static, bodyType)));
		bodyType->GetContextMenu()->AddItem(new ContextItem(L"Kinematic", std::bind(&RigidBody::SetBodyTypeFromWidget, this, IRigidBody::BodyType::Kinematic, bodyType)));
		bodyType->GetContextMenu()->AddItem(new ContextItem(L"Dynamic", std::bind(&RigidBody::SetBodyTypeFromWidget, this, IRigidBody::BodyType::Dynamic, bodyType)));

		DropDown* colliderType = new DropDown(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Collider");
		colliderType->SetPrefabOverrideBinding(GetComponentName(), "/_shape");

		switch (_colliderShape)
		{
		case IRigidBody::ColliderShape::Box: colliderType->SetValue(L"Box"); break;
		case IRigidBody::ColliderShape::Capsule: colliderType->SetValue(L"Capsule"); break;
		case IRigidBody::ColliderShape::HeightField: colliderType->SetValue(L"Heightfield"); break;
		case IRigidBody::ColliderShape::Sphere: colliderType->SetValue(L"Sphere"); break;
		case IRigidBody::ColliderShape::TriangleMesh: colliderType->SetValue(L"Triangle Mesh"); break;

		case IRigidBody::ColliderShape::None:
		default:
			colliderType->SetValue(L"None"); break;
		}
		
		colliderType->SetLabelMinSize(130);
		colliderType->GetContextMenu()->Disable();
		colliderType->GetContextMenu()->AddItem(new ContextItem(L"None", std::bind(&RigidBody::RemoveCollider, this)));
		colliderType->GetContextMenu()->AddItem(new ContextItem(L"Box", std::bind(&RigidBody::AddBoxColliderFromWidget, this, colliderType)));
		colliderType->GetContextMenu()->AddItem(new ContextItem(L"Sphere", std::bind(&RigidBody::AddSphereColliderFromWidget, this, colliderType)));
		colliderType->GetContextMenu()->AddItem(new ContextItem(L"Capsule", std::bind(&RigidBody::AddCapsuleColliderFromWidget, this, colliderType)));
		colliderType->GetContextMenu()->AddItem(new ContextItem(L"Triangle Mesh", std::bind(&RigidBody::AddTriangleColliderFromWidget, this, colliderType)));

		Checkbox* isTrigger = new Checkbox(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Is trigger?", &_isTrigger);
		isTrigger->SetPrefabOverrideBinding(GetComponentName(), "/_isTrigger");
		isTrigger->SetOnCheckFn(std::bind(&RigidBody::OnSetIsTriggerFromWidget, this, std::placeholders::_2));

		Checkbox* isGravity = new Checkbox(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Apply gravity?", &_isGravityApplied);
		isGravity->SetPrefabOverrideBinding(GetComponentName(), "/_isGravityApplied");
		isGravity->SetOnCheckFn(std::bind(&RigidBody::OnSetIsGravityFromWidget, this, std::placeholders::_2));

		if(_rigidBody)
			_massAdjust = _rigidBody->GetMass();
		DragFloat* mass = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Mass", &_massAdjust, 1.0f, 10000.0f, 0.5f);
		mass->SetPrefabOverrideBinding(GetComponentName(), "/_mass");
		mass->SetOnDrag(std::bind(&RigidBody::SetMass, this, std::placeholders::_1));

		//Button* addCollider = new Button(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Add Collider", std::bind(&RigidBody::AddColliderFromWidget,

		return true;
	}

	void RigidBody::SetMass(float mass)
	{
		_rigidBody->SetMass(mass);
		_massAdjust = mass;
	}

	void RigidBody::SetBodyTypeFromWidget(IRigidBody::BodyType type, DropDown* element)
	{
		if (!_rigidBody)
			return;

		_rigidBody->SetBodyType(type);

		if (type == IRigidBody::BodyType::Dynamic)
		{
			_rigidBody->SetGravityEnabled(_isGravityApplied);
			_rigidBody->SetMass(_massAdjust);
		}
		
		if (element && element->GetContextMenu())
			element->GetContextMenu()->Disable();
	}

	void RigidBody::AddTriangleColliderFromWidget(DropDown* widget)
	{
		if (_colliderShape == IRigidBody::ColliderShape::TriangleMesh)
			return;

		RemoveCollider();

		int c = 0;
		auto mesh = GetEntity()->GetComponent<HexEngine::StaticMeshComponent>()->GetMesh();
		{
			AddTriangleMeshCollider(mesh.get(), true);
		}

		widget->SetValue(L"Triangle Mesh");
	}

	void RigidBody::AddBoxColliderFromWidget(DropDown* widget)
	{
		if (_colliderShape == IRigidBody::ColliderShape::Box)
			return;

		RemoveCollider();

		AddBoxCollider(GetScaledEntityAABB(GetEntity()));

		widget->SetValue(L"Box");
	}

	void RigidBody::AddSphereColliderFromWidget(DropDown* widget)
	{
		if (_colliderShape == IRigidBody::ColliderShape::Sphere)
			return;

		RemoveCollider();

		// Enclosing sphere: the largest half-extent of the scaled AABB.
		const math::Vector3 ext = GetScaledEntityAABB(GetEntity()).Extents;
		const float radius = std::max(ext.x, std::max(ext.y, ext.z));
		if (radius <= 0.0f)
			return;

		AddSphereCollider(radius);

		widget->SetValue(L"Sphere");
	}

	void RigidBody::AddCapsuleColliderFromWidget(DropDown* widget)
	{
		if (_colliderShape == IRigidBody::ColliderShape::Capsule)
			return;

		RemoveCollider();

		// Fit a capsule to the scaled AABB, aligning its long axis with the
		// entity's longest dimension: radius spans the two shorter axes, the
		// cylindrical section fills the remainder of the longest. This gives a
		// natural capsule for both a standing character (tall in Y) and a
		// vehicle chassis (long in Z) without the author picking an axis.
		const math::Vector3 ext = GetScaledEntityAABB(GetEntity()).Extents; // half-extents
		int axis = 1;
		if (ext.x >= ext.y && ext.x >= ext.z) axis = 0;
		else if (ext.z >= ext.x && ext.z >= ext.y) axis = 2;
		else axis = 1;

		float halfLong;
		float r;
		if (axis == 0) { halfLong = ext.x; r = std::max(ext.y, ext.z); }
		else if (axis == 2) { halfLong = ext.z; r = std::max(ext.x, ext.y); }
		else { halfLong = ext.y; r = std::max(ext.x, ext.z); }

		if (r <= 0.0f)
			return;

		// PxCapsuleGeometry(height) is the cylinder segment between the two
		// hemispherical caps, so subtract a full radius from the long span.
		const float height = std::max((halfLong - r) * 2.0f, 0.0f);

		AddCapsuleCollider(r, height, axis);

		widget->SetValue(L"Capsule");
	}

	void RigidBody::OnSetIsTriggerFromWidget(bool value)
	{
		GetIRigidBody()->SetIsTrigger(value);
		_isTrigger = value;

		GetEntity()->SetLayer(value ? Layer::Trigger : Layer::StaticGeometry);
	}

	void RigidBody::OnSetIsGravityFromWidget(bool value)
	{
		GetIRigidBody()->SetGravityEnabled(value);
		_isGravityApplied = value;
	}

	void RigidBody::CreateCharacterController(const ControllerParameters& params, Transform* transform)
	{
		if (_rigidBody != nullptr)
		{
			RemoveCollider();

			g_pEnv->_physicsSystem->DestroyRigidBody(_rigidBody);			
		}

		if (transform)
			transform->EnableInterpolation(true);

		_rigidBody = g_pEnv->_physicsSystem->CreateController(params, transform);
	}
}
