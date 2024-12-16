

#include "Tree.hpp"
#include "../../HexEngine.Core/Environment/IEnvironment.hpp"
#include "../../HexEngine.Core/Entity/Component/MeshRenderer.hpp"

#include <random>
#include "easing.h"

namespace CityBuilder
{
	Tree::Tree()
	{
		SetName("Tree");
		SetLayer(HexEngine::Layer::DynamicGeometry);
	}

	float get_random()
	{
		static std::default_random_engine e;
		static std::uniform_real_distribution<> dis(0, 1); // rage 0 - 1
		return dis(e);
	}

	void Tree::Destroy()
	{
		Entity::Destroy();

		for (auto&& foliage : _foliage)
		{
			HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->RemoveEntity(foliage);
			SAFE_DELETE(foliage);
		}

		SAFE_UNLOAD(_treeModel);
	}

	void Tree::Update(float frameTime)
	{
		Entity::Update(frameTime);		

		if (_scale > 1.0f)
		{
			_scale = 1.0f;

			// once we reached this point we are no longer dynamic and can be changed to static geometry
			SetLayer(HexEngine::Layer::StaticGeometry);
			return;
		}

		_scale += frameTime * 1.15f;

		auto easingFunction = getEasingFunction(EaseOutElastic);
		double progress = easingFunction(_scale) * _targetScale;	// 0.058

		GetComponent<HexEngine::Transform>()->SetScale(math::Vector3(progress));

		for (auto&& foliage : _foliage)
		{
			foliage->GetComponent<HexEngine::Transform>()->SetScale(math::Vector3(progress));
		}
	}

	Tree* Tree::Create(const math::Vector3& position)
	{
		float rotation = get_random() * 360.0f;
		float scale = 2.0f + (get_random() * 8.0f);

		// Create the initial tree
		//
		Tree* tree = new Tree;

		tree->_treeModel = (HexEngine::Model*)HexEngine::g_pEnv->_resourceSystem->LoadResource("Models/World/Nature/Tree001.obj");

		HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->AddEntity(tree);

		tree->_targetScale = scale;

		// set the position
		//
		tree->SetPosition(position);

		for (auto i = 0ul; i < tree->_treeModel->GetNumMeshes(); ++i)
		{
			auto modelMesh = tree->_treeModel->GetMeshAtIndex(i);

			if (!modelMesh)
				continue;

			if (modelMesh->GetName().find("foliage") != std::string::npos)
			{
				tree->CreateFoliage(modelMesh, position, scale, rotation);
				continue;
			}

			auto meshComponent = tree->AddComponent<HexEngine::MeshRenderer>();

			// Create the new mesh from the Model's mesh data
			//
			auto mesh = modelMesh->Clone();

			// Set the mesh in the renderer
			//
			meshComponent->SetMesh(mesh);

			// Create a mesh instance
			//
			mesh->CreateBuffers(false);

			HexEngine::MaterialProps material = {};

			material.diffuseColour = math::Vector4(RGBA_TO_FLOAT4(159, 84, 38, 255));
			material.shininessStrength = 0.14f;

			mesh->SetMaterialProps(material);
			
			tree->SetScale(math::Vector3(0.0f));
			tree->SetRotation(math::Quaternion::CreateFromAxisAngle(math::Vector3(0.0f, 1.0f, 0.0f), ToRadian(rotation)));
		}		

		return tree;
	}

	void Tree::CreateFoliage(HexEngine::Mesh* mesh, const math::Vector3& position, float scale, float rotation)
	{
		auto foliage = new Foliage;

		//foliage->_targetFoliageScale = scale;

		auto meshComponent = foliage->AddComponent<HexEngine::MeshRenderer>();

		// Create the new mesh from the Model's mesh data
		//
		auto foliageMesh = mesh->Clone();

		// Set the mesh in the renderer
		//
		meshComponent->SetMesh(foliageMesh);

		// Create an mesh instance for this mesh
		//
		foliageMesh->CreateInstance();

		// Add the entity to the scene
		//
		HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->AddEntity(foliage);

		// Add to the tree's foliage
		//
		_foliage.push_back(foliage);

		HexEngine::MaterialProps material = {};

		material.diffuseColour = math::Vector4(get_random(), get_random(), get_random(), 1.0f);
		material.shininessStrength = 0.5f;

		foliageMesh->SetMaterialProps(material);

		// set the scale to 0 to begin with so we can animate the foliage
		//
		foliage->SetScale(math::Vector3(0.0f));
		foliage->SetRotation(math::Quaternion::CreateFromAxisAngle(math::Vector3(0.0f, 1.0f, 0.0f), ToRadian(rotation)));
		foliage->SetPosition(position);
	}

	void Tree::Create()
	{
		Entity::Create();

		//for (auto i = 0ul; i < model->GetNumMeshes(); ++i)
		//{
		//	auto meshComponent = AddComponent<HexEngine::MeshRenderer>();

		//	meshComponent->SetMesh(model->GetMeshAtIndex(i));

		//	//GetComponent<HexEngine::Transform>()->SetScale(math::Vector3(2.0f));
		//	//SetRotation(math::Quaternion::CreateFromAxisAngle(math::Vector3(0.0f, 1.0f, 0.0f), ToRadian(rand() % 360)));
		//}
	}
}