

#include "AssimpModelSystem.hpp"
#include <HexEngine.Core\Graphics\MaterialLoader.hpp>

#define AI2VEC2(val) math::Vector2(val.x, val.y);
#define AI2VEC3(val) math::Vector3(val.x, val.y, val.z);
#define AI2VEC4(val) math::Vector4(val.x, val.y, val.z, 1.0f);

namespace HexEngine
{
	const uint32_t AssimpImportFlags =
#if 0
		//aiProcessPreset_TargetRealtime_Quality |
		aiProcess_CalcTangentSpace |
		aiProcess_GenSmoothNormals |
		//aiProcess_JoinIdenticalVertices         |
		//aiProcess_ImproveCacheLocality          |
		//aiProcess_LimitBoneWeights              |
		//aiProcess_RemoveRedundantMaterials      |
		//aiProcess_SplitLargeMeshes              |
		aiProcess_Triangulate |
		//aiProcess_GenUVCoords                   |
		aiProcess_SortByPType |
		//aiProcess_FindDegenerates               |
		//aiProcess_FindInvalidData               |
		aiProcess_PreTransformVertices | // fix for fbx rotation bug?
		//aiProcess_FlipUVs | 
		aiProcess_FixInfacingNormals | // fix for incorrect nornmals on FBX
		//aiProcess_ConvertToLeftHanded

		//aiProcess_MakeLeftHanded |
		aiProcess_FlipUVs | 
		//aiProcess_FlipWindingOrder  |

		//aiProcess_OptimizeMeshes | aiProcess_OptimizeGraph //| aiProcess_FindInstances
		0
#else
		aiProcess_CalcTangentSpace | // calculate tangents and bitangents if possible
		aiProcess_JoinIdenticalVertices | // join identical vertices/ optimize indexing
		//aiProcess_ValidateDataStructure  | // perform a full validation of the loader's output
		aiProcess_Triangulate | // Ensure all verticies are triangulated (each 3 vertices are triangle)
		aiProcess_FlipUVs | // convert everything to D3D left handed space (by default right-handed, for OpenGL)
		aiProcess_SortByPType | // ?
		aiProcess_ImproveCacheLocality | // improve the cache locality of the output vertices
		aiProcess_RemoveRedundantMaterials | // remove redundant materials
		aiProcess_FindDegenerates | // remove degenerated polygons from the import
		aiProcess_FindInvalidData | // detect invalid model data, such as invalid normal vectors
		aiProcess_GenUVCoords | // convert spherical, cylindrical, box and planar mapping to proper UVs
		aiProcess_TransformUVCoords | // preprocess UV transformations (scaling, translation ...)
		aiProcess_FindInstances | // search for instanced meshes and remove them by references to one master
		aiProcess_LimitBoneWeights | // limit bone weights to 4 per vertex
		aiProcess_OptimizeMeshes | // join small meshes, if possible;

		//aiProcess_SplitLargeMeshes |
		aiProcess_PreTransformVertices |

		aiProcess_SplitByBoneCount | // split meshes with too many bones. Necessary for our (limited) hardware skinning shader
		//aiProcess_MakeLeftHanded |
		0
#endif
		;

	AssimpModelImporter::AssimpModelImporter()
	{
		g_pEnv->_resourceSystem->RegisterResourceLoader(this);
	}

	AssimpModelImporter::~AssimpModelImporter()
	{
		g_pEnv->_resourceSystem->UnregisterResourceLoader(this);
	}

	void AssimpModelImporter::Destroy()
	{
	}

	Dialog* AssimpModelImporter::CreateEditorDialog(const fs::path& path, FileSystem* fileSystem)
	{
		const int32_t width = 800;
		const int32_t height = 600;

		Dialog* dlg = new Dialog(
			g_pEnv->_uiManager->GetRootElement(),
			Point::GetScreenCenterWithOffset(-width / 2, -height / 2),
			Point(width, height),
			std::format(L"Importing model: {}", path.filename().wstring()));

		dlg->BringToFront();

		auto layout = new ComponentWidget(dlg,
			Point(10, 10),
			Point(dlg->GetSize().x - 20, dlg->GetSize().y - 20),
			L"");

		auto importAnims = new Checkbox(layout, layout->GetNextPos(), Point(200, 20), L"Import animations", &_importOpts.importAnimations);
		auto createAnims = new Checkbox(layout, layout->GetNextPos(), Point(200, 20), L"Create materials", &_importOpts.tryAndCreateMaterials);

		auto searchpath = new LineEdit(layout, layout->GetNextPos(), Point(500, 20), L"Override texture search path");
		

		//searchpath->SetUneditableText(fileSystem->GetName());
		searchpath->SetDoesCallbackWaitForReturn(false);
		searchpath->SetValue(_importOpts.textureSearchPath);
		searchpath->SetOnInputFn([&](LineEdit*, const std::wstring& text) {
			_importOpts.textureSearchPath = text;
			});

		auto textureExtension = new LineEdit(layout, layout->GetNextPos(), Point(500, 20), L"Override texture extension");
		textureExtension->SetDoesCallbackWaitForReturn(false);
		textureExtension->SetValue(_importOpts.replaceTextureExtension);
		textureExtension->SetOnInputFn([&](LineEdit*, const std::wstring& text) {
			_importOpts.replaceTextureExtension = text;
			});

		auto import = new Button(layout, layout->GetNextPos(), Point(80, 20), L"Import", 
			std::bind(&AssimpModelImporter::LoadResourceFromFile, this, fileSystem->GetLocalAbsoluteDataPath(path), fileSystem, nullptr));

		return dlg;
	}

	Model* AssimpModelImporter::LoadResourceFromFile(const fs::path& path, FileSystem* fileSystem, const ResourceLoadOptions* options /*= nullptr*/)
	{
		if (path.extension() == ".mtl")
			return nullptr;

		Assimp::Importer importer;

		LOG_DEBUG("Loading model '%S'", path.filename().c_str());

		std::wstring str = path;

		std::string newPath;
		std::transform(str.begin(), str.end(), std::back_inserter(newPath), [](wchar_t c) {
			return (char)c;
			});

		const aiScene* modelScene = importer.ReadFile(newPath, AssimpImportFlags);

		if (modelScene == nullptr)
		{
			
			LOG_WARN("Failed to load model: %s!", importer.GetErrorString());
			return nullptr;
		}

		_currentPath = path;

		if (modelScene->mMetaData)
		{
			int upAxis = 1;
			modelScene->mMetaData->Get<int>("UpAxis", upAxis);
			int upAxisSign = 1;
			modelScene->mMetaData->Get<int>("UpAxisSign", upAxisSign);

			int frontAxis = 2;
			modelScene->mMetaData->Get<int>("FrontAxis", frontAxis);
			int frontAxisSign = 1;
			modelScene->mMetaData->Get<int>("FrontAxisSign", frontAxisSign);

			int coordAxis = 0;
			modelScene->mMetaData->Get<int>("CoordAxis", coordAxis);
			int coordAxisSign = 1;
			modelScene->mMetaData->Get<int>("CoordAxisSign", coordAxisSign);

			int scaleFactor = 1;
			modelScene->mMetaData->Get<int>("UnitScaleFactor", scaleFactor);

			int originalScaleFactor = 1;
			modelScene->mMetaData->Get<int>("OriginalUnitScaleFactor", originalScaleFactor);

			

			aiVector3D upVec = upAxis == 0 ? aiVector3D(upAxisSign, 0, 0) : upAxis == 1 ? aiVector3D(0, upAxisSign, 0) : aiVector3D(0, 0, upAxisSign);
			aiVector3D forwardVec = frontAxis == 0 ? aiVector3D(frontAxisSign, 0, 0) : frontAxis == 1 ? aiVector3D(0, frontAxisSign, 0) : aiVector3D(0, 0, frontAxisSign);
			aiVector3D rightVec = coordAxis == 0 ? aiVector3D(coordAxisSign, 0, 0) : coordAxis == 1 ? aiVector3D(0, coordAxisSign, 0) : aiVector3D(0, 0, coordAxisSign);
			aiMatrix4x4 mat(rightVec.x, rightVec.y, rightVec.z, 0.0f,
				upVec.x, upVec.y, upVec.z, 0.0f,
				forwardVec.x, forwardVec.y, forwardVec.z, 0.0f,
				0.0f, 0.0f, 0.0f, 1.0f);

			modelScene->mRootNode->mTransformation *= mat;
		}

		Model* model = new Model;

		//if(_importAnimations)
		//	ProcessAnimations(model, modelScene);

		std::vector<AnimChannel*> parents(modelScene->mNumAnimations);

		ProcessNode(model, modelScene->mRootNode, parents, modelScene, fileSystem);

		/*if (modelScene->mNumAnimations > 0)
		{
			for (auto& mesh : model->GetMeshes())
			{
				((AnimatedMesh*)mesh)->_animData = model->_animData;

				
			}
		}*/
		// Update the max lod levels
		for (auto& mesh : model->GetMeshes())
		{
			mesh->SetMaxLodLevel(model->GetMaxLOD());

			mesh->_modelTransform = *(math::Matrix*)&modelScene->mRootNode->mTransformation.a1;
			mesh->_modelTransform = mesh->_modelTransform.Transpose();
		}

		//if (options)
		//{
		//	ModelLoadOptions* modelOpts = (ModelLoadOptions*)options;

		//	if (modelOpts->loadLights == true && modelScene->HasLights())
		//	{
		//		for (int i = 0; i < modelScene->mNumLights; ++i)
		//		{
		//			auto light = modelScene->mLights[i];

		//			switch (light->mType)
		//			{
		//			case aiLightSourceType::aiLightSource_POINT:
		//			{
		//				LOG_DEBUG("Creating a point light from model scene");

		//				auto lightNode = modelScene->mRootNode->FindNode(light->mName);

		//				math::Vector3 lightPos = /*AI2VEC3(light->mPosition) + */math::Vector3(lightNode->mTransformation.a4, lightNode->mTransformation.b4, lightNode->mTransformation.c4);

		//				SpotLight* spotLight = new SpotLight(lightNode->mTransformation.a1 / 10.0f);
		//				spotLight->SetPosition(lightPos);
		//				spotLight->SetDiffuseColour(math::Vector4(light->mColorDiffuse.r, light->mColorDiffuse.g, light->mColorDiffuse.b, 1.0f));

		//				g_pEnv->_sceneManager->GetCurrentScene()->CreateEntity(light->mName, lightPos);
		//				break;
		//			}
		//			}
		//		}
		//	}
		//}

		return model;
	}

	Model* AssimpModelImporter::LoadResourceFromMemory(const std::vector<uint8_t>& data, const fs::path& relativePath, FileSystem* fileSystem, const ResourceLoadOptions* options)
	{
		if (relativePath.extension() == ".mtl")
			return nullptr;

		Assimp::Importer importer;

		_defaultIoSystem = importer.GetIOHandler();

		importer.SetIOHandler(new AssimpIoSystem);

		LOG_DEBUG("Loading model '%S'", relativePath.filename().c_str());

		std::wstring str = relativePath;

		std::string newPath;
		std::transform(str.begin(), str.end(), std::back_inserter(newPath), [](wchar_t c) {
			return (char)c;
			});

		std::string hint = relativePath.extension().string();

		/*const aiScene* modelScene = importer.ReadFileFromMemory(
			data.data(),
			data.size(),
			AssimpImportFlags,
			newPath.c_str()
		);*/

		const aiScene* modelScene = importer.ReadFile(
			newPath,
			AssimpImportFlags
		);

		if (modelScene == nullptr)
		{
			LOG_CRIT("Failed to load model: %s!", importer.GetErrorString());
			return nullptr;
		}

		_currentPath = relativePath;

		Model* model = new Model;
		model->SetPaths(relativePath, fileSystem);

		/*if (modelScene->mMetaData)
		{
			int upAxis = 1;
			modelScene->mMetaData->Get<int>("UpAxis", upAxis);
			int upAxisSign = 1;
			modelScene->mMetaData->Get<int>("UpAxisSign", upAxisSign);

			int frontAxis = 2;
			modelScene->mMetaData->Get<int>("FrontAxis", frontAxis);
			int frontAxisSign = 1;
			modelScene->mMetaData->Get<int>("FrontAxisSign", frontAxisSign);

			int coordAxis = 0;
			modelScene->mMetaData->Get<int>("CoordAxis", coordAxis);
			int coordAxisSign = 1;
			modelScene->mMetaData->Get<int>("CoordAxisSign", coordAxisSign);



			aiVector3D upVec = upAxis == 0 ? aiVector3D(upAxisSign, 0, 0) : upAxis == 1 ? aiVector3D(0, upAxisSign, 0) : aiVector3D(0, 0, upAxisSign);
			aiVector3D forwardVec = frontAxis == 0 ? aiVector3D(frontAxisSign, 0, 0) : frontAxis == 1 ? aiVector3D(0, frontAxisSign, 0) : aiVector3D(0, 0, frontAxisSign);
			aiVector3D rightVec = coordAxis == 0 ? aiVector3D(coordAxisSign, 0, 0) : coordAxis == 1 ? aiVector3D(0, coordAxisSign, 0) : aiVector3D(0, 0, coordAxisSign);
			aiMatrix4x4 mat(rightVec.x, rightVec.y, rightVec.z, 0.0f,
				upVec.x, upVec.y, upVec.z, 0.0f,
				forwardVec.x, forwardVec.y, forwardVec.z, 0.0f,
				0.0f, 0.0f, 0.0f, 1.0f);

			modelScene->mRootNode->mTransformation *= mat;
		}*/

		ProcessAnimations(model, modelScene);

		std::vector<AnimChannel*> parentAnims(modelScene->mNumAnimations);

		ProcessNode(model, modelScene->mRootNode, parentAnims, modelScene, fileSystem);

		/*for (auto& mesh : model->GetMeshes())
		{
			if(model->_animData)
				((AnimatedMesh*)mesh)->_animData = model->_animData;
		}*/
		// Update the max lod levels
		for (auto& mesh : model->GetMeshes())
		{
			mesh->SetMaxLodLevel(model->GetMaxLOD());
		}

		//if (options)
		//{
		//	ModelLoadOptions* modelOpts = (ModelLoadOptions*)options;

		//	if (modelOpts->loadLights == true && modelScene->HasLights())
		//	{
		//		for (int i = 0; i < modelScene->mNumLights; ++i)
		//		{
		//			auto light = modelScene->mLights[i];

		//			switch (light->mType)
		//			{
		//			case aiLightSourceType::aiLightSource_POINT:
		//			{
		//				LOG_DEBUG("Creating a point light from model scene");

		//				auto lightNode = modelScene->mRootNode->FindNode(light->mName);

		//				math::Vector3 lightPos = /*AI2VEC3(light->mPosition) + */math::Vector3(lightNode->mTransformation.a4, lightNode->mTransformation.b4, lightNode->mTransformation.c4);

		//				SpotLight* spotLight = new SpotLight(lightNode->mTransformation.a1 / 10.0f);
		//				spotLight->SetPosition(lightPos);
		//				spotLight->SetDiffuseColour(math::Vector4(light->mColorDiffuse.r, light->mColorDiffuse.g, light->mColorDiffuse.b, 1.0f));

		//				g_pEnv->_sceneManager->GetCurrentScene()->CreateEntity(light->mName, lightPos);
		//				break;
		//			}
		//			}
		//		}
		//	}
		//}

		return model;
	}

	void AssimpModelImporter::ProcessAnimations(Model* model, const aiScene* scene)
	{
		if (!scene->mNumAnimations)
			return;

		model->_animData = std::make_shared<AnimationData>();

		model->_animData->_globalInverseTransform = math::Matrix(&scene->mRootNode->mTransformation.a1);
		model->_animData->_globalInverseTransform = model->_animData->_globalInverseTransform.Invert(); // do we need this?


		for (auto i = 0u; i < scene->mNumAnimations; ++i)
		{
			auto animation = scene->mAnimations[i];

			if (!animation)
				continue;

			Animation anim;
			anim.name = animation->mName.data;
			anim.duration = animation->mDuration;
			anim.ticksPerSecond = animation->mTicksPerSecond;

			for (int32_t j = 0; j < animation->mNumChannels; ++j)
			{
				AnimChannel chan;
				chan.nodeName = animation->mChannels[j]->mNodeName.data;

				for (int32_t p = 0; p < animation->mChannels[j]->mNumPositionKeys; ++p)
				{
					auto& key = animation->mChannels[j]->mPositionKeys[p];
					chan.positionKeys.push_back({ key.mTime, math::Vector3(key.mValue.x, key.mValue.y, key.mValue.z) });
				}

				for (int32_t p = 0; p < animation->mChannels[j]->mNumRotationKeys; ++p)
				{
					auto& key = animation->mChannels[j]->mRotationKeys[p];
					chan.rotationKeys.push_back({ key.mTime, math::Quaternion(key.mValue.x, key.mValue.y, key.mValue.z, key.mValue.w) });
				}

				for (int32_t p = 0; p < animation->mChannels[j]->mNumScalingKeys; ++p)
				{
					auto& key = animation->mChannels[j]->mScalingKeys[p];
					chan.scaleKeys.push_back({ key.mTime, math::Vector3(key.mValue.x, key.mValue.y, key.mValue.z) });
				}

				anim.channels.push_back(chan);
			}

			model->_animData->_animations.push_back(anim);
		}
	}

	std::vector<std::string> AssimpModelImporter::GetSupportedResourceExtensions()
	{
		return { ".obj", ".fbx", ".blend", ".mtl", ".glb", ".gltf", ".max"};
	}

	std::wstring AssimpModelImporter::GetResourceDirectory() const
	{
		return L"Models";
	}

	void AssimpModelImporter::UnloadResource(IResource* resource)
	{
		SAFE_DELETE(resource);
	}

	void AssimpModelImporter::ProcessNode(Model* model, aiNode* node, std::vector<AnimChannel*> parentAnims, const aiScene* scene, FileSystem* fileSystem)
	{
		if (model->_animData && _importOpts.importAnimations)
		{
			int32_t animIdx = 0;

			for (auto& anim : model->_animData->_animations)
			{
				bool didFind = false;

				for (auto& chan : anim.channels)
				{
					if (chan.nodeName == node->mName.data)
					{
						didFind = true;

						chan.nodeTransform = math::Matrix(&node->mTransformation.a1);
						//chan.transform = chan.transform.Invert();

						if (parentAnims[animIdx] != nullptr)
						{
							LOG_DEBUG("Anim%d '%s': Node '%s' became parent to '%s'", animIdx, anim.name.c_str(), parentAnims[animIdx]->nodeName.c_str(), chan.nodeName.c_str());

							parentAnims[animIdx]->children.push_back(&chan);
							parentAnims[animIdx] = &chan;
						}
						else
						{
							anim._rootNode = &chan;
							parentAnims[animIdx] = &chan;

							LOG_DEBUG("Anim%d '%s': Node '%s' became the root", animIdx, anim.name.c_str(), parentAnims[animIdx]->nodeName.c_str());
						}

						//anim.nodeToAnimMap[chan.nodeName] = &chan;
						break;
					}
				}

				if (!didFind)
				{
					bool a = false;
				}
				//break;

				animIdx++;
			}
		}

		/*auto anim = FindAnimChannelFromNodeName(model, node->mName.data);

		if (anim)
		{
			anim->transform = math::Matrix(&node->mTransformation.a1);

			if (parentAnims[0])
			{
				parentAnims[0]->children.push_back(anim);
				parentAnims[0] = anim;
			}
			else
			{
				model->_animData._animations[0]._rootNode = anim;
				parentAnims[0] = anim;
			}

			model->_animData._animations[0].nodeToAnimMap[anim->nodeName] = anim;
		}*/

		/*for (auto& anim : model->_animations)
		{
			for (auto& chan : anim.channels)
			{
				if (chan.nodeName == node->mName.data)
				{
					chan.transform = math::Matrix(&node->mTransformation.a1);

					if (!parentAnim)
					{
						model->_rootNode = &chan;
					}
					else
					{
						parentAnim->children.push_back(&chan);
					}

					auto it = model->_nodeToAnimMap.find(node->mName.data);

					if (it == model->_nodeToAnimMap.end())
					{
						model->_nodeToAnimMap[chan.nodeName] = &chan;
					}
				}
			}
		}*/

		

		for (uint32_t i = 0; i < node->mNumMeshes; ++i)
		{
			aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
						
			//if(scene->HasAnimations())
			//	ProcessAnimatedMesh(mesh, scene, node, fileSystem);
			//else
				ProcessMesh(model, mesh, scene, node, fileSystem);
		}

		for (uint32_t i = 0; i < node->mNumChildren; ++i)
		{
			ProcessNode(model, node->mChildren[i], parentAnims, scene, fileSystem);
		}
	}

	AnimChannel* AssimpModelImporter::FindAnimChannelFromNodeName(Model* model, const std::string& nodeName)
	{
		for (auto& anim : model->_animData->_animations)
		{
			for (auto& chan : anim.channels)
			{
				if (chan.nodeName == nodeName)
				{
					return &chan;
				}
			}
		}
		return nullptr;
	}

	Mesh* AssimpModelImporter::ProcessMesh(Model* model, aiMesh* mesh, const aiScene* scene, aiNode* node, FileSystem* fileSystem)
	{
		std::string meshName = node->mName.C_Str();

		meshName.erase(std::remove(meshName.begin(), meshName.end(), ':'));
		meshName.erase(std::remove(meshName.begin(), meshName.end(), '.'));

		meshName.append("_");
		meshName.append(std::to_string(mesh->mNumVertices));
		meshName.append("_");
		meshName.append(std::to_string(mesh->mNumFaces));

		// Pre-calculate needed vertices and indices
		//
		Mesh* modelMesh = new Mesh(model, meshName);

		auto fixedExtensionPath = _currentPath;

		fixedExtensionPath.replace_filename(_currentPath.stem().string() + "_" + meshName);
		fixedExtensionPath.replace_extension(".hmesh");

		

		//modelMesh->AddRef();
		modelMesh->SetPaths(fixedExtensionPath, fileSystem);
		modelMesh->SetLoader(g_pEnv->_meshLoader);
		modelMesh->_faceCount = mesh->mNumFaces;

		uint32_t totalIndices = 0;
		uint32_t totalVertices = mesh->mNumVertices;

		model->AddMesh(modelMesh);

		modelMesh->_vertices.reserve(totalVertices);

		for (auto i = 0U; i < mesh->mNumFaces; ++i)
		{
			totalIndices += mesh->mFaces[i].mNumIndices;
		}

		LOG_DEBUG("Mesh '%s' has %d vertices and %d indices", mesh->mName.C_Str(), totalVertices, totalIndices);

		// calculate transformation
		auto transformation = node->mTransformation;

		auto parent = node->mParent;
		while (parent)
		{
			transformation *= parent->mTransformation;
			parent = parent->mParent;
		}

		math::Matrix transmat = *(math::Matrix*)&transformation.a1;
		transmat = transmat.Transpose();

		std::vector<math::Vector4> points;		

		if(totalIndices > 0)
			modelMesh->_indices.reserve(totalIndices);

#if 1 // old broken method of getting faces
		for (auto i = 0U; i < mesh->mNumVertices; ++i)
		{
			MeshVertex vertex;

			vertex._position = AI2VEC4(mesh->mVertices[i]);
			//vertex._position = math::Vector4::Transform(vertex._position, transmat);

			points.push_back(vertex._position);

			if (mesh->mTextureCoords[0])
			{
				vertex._texcoord = AI2VEC2(mesh->mTextureCoords[0][i]);
			}

			// set up the tangent and bitangents
			if (mesh->mTangents != nullptr)
			{
				vertex._tangent = AI2VEC3(mesh->mTangents[i]);
				vertex._bitangent = AI2VEC3(mesh->mBitangents[i]);

				//vertex._tangent = math::Vector3::Transform(vertex._tangent, transmat);
				//vertex._bitangent = math::Vector3::Transform(vertex._bitangent, transmat);
			}

			if (mesh->mNormals != nullptr)
			{
				vertex._normal = AI2VEC3(mesh->mNormals[i]);
				//vertex._normal = math::Vector3::Transform(vertex._normal, transmat);
			}


			modelMesh->AddVertex(vertex);
		}
#endif

		
		for (uint32_t i = 0; i < mesh->mNumFaces; ++i)
		{
			aiFace& face = mesh->mFaces[i];

			/*if (face.mNumIndices != 3) {
				continue;
			}*/

			for (uint32_t j = 0; j < face.mNumIndices; ++j)
			{
				const auto index = face.mIndices[j];

				modelMesh->_indices.push_back((MeshIndexFormat)index);

#if 0
				MeshVertex vertex;

				vertex._position = AI2VEC4(mesh->mVertices[index]);
				//vertex._position = math::Vector4::Transform(vertex._position, transmat);

				// store the raw vertex data too, we might need it for physics
				modelMesh->_rawVertices.push_back(math::Vector3(vertex._position.x, vertex._position.y, vertex._position.z));

				points.push_back(vertex._position);

				if (mesh->mTextureCoords[0])
				{
					vertex._texcoord = AI2VEC2(mesh->mTextureCoords[0][index]);
				}

				// set up the tangent and bitangents
				if (mesh->mTangents != nullptr)
				{
					vertex._tangent = AI2VEC3(mesh->mTangents[index]);
					vertex._bitangent = AI2VEC3(mesh->mBitangents[index]);

					//vertex._tangent = math::Vector3::Transform(vertex._tangent, transmat);
					//vertex._bitangent = math::Vector3::Transform(vertex._bitangent, transmat);
				}

				if (mesh->mNormals != nullptr)
				{
					vertex._normal = AI2VEC3(mesh->mNormals[index]);
					//vertex._normal = math::Vector3::Transform(vertex._normal, transmat);
				}


				modelMesh->AddVertex(vertex);
#endif
			}
		}

		// Construct the bounding boxes
		dx::BoundingBox::CreateFromPoints(modelMesh->_aabb, points.size(), (const math::Vector3*)points.data(), sizeof(math::Vector4));
		dx::BoundingOrientedBox::CreateFromBoundingBox(modelMesh->_obb, modelMesh->_aabb);


		if (_importOpts.tryAndCreateMaterials && mesh->mMaterialIndex >= 0)
		{
			auto material = scene->mMaterials[mesh->mMaterialIndex];

			if (material)
			{
				modelMesh->_materialName = material->GetName().C_Str();
				ProcessMaterial(modelMesh, scene, material, fileSystem);
			}
		}

		modelMesh->Save();

		return modelMesh;
	}

	AnimatedMesh* AssimpModelImporter::ProcessAnimatedMesh(Model* model, aiMesh* mesh, const aiScene* scene, aiNode* node, FileSystem* fileSystem)
	{
		// Pre-calculate needed vertices and indices
		//
		AnimatedMesh* modelMesh = new AnimatedMesh(model, node->mName.C_Str());

		//modelMesh->AddRef();
		modelMesh->SetPaths(_currentPath, fileSystem);
		modelMesh->_faceCount = mesh->mNumFaces;

		// set the root transformation for this node
		modelMesh->_rootTransformation = math::Matrix(&node->mTransformation.a1);

		uint32_t totalIndices = 0;
		uint32_t totalVertices = mesh->mNumVertices;

		LOG_DEBUG("Mesh '%s' has %d vertices", mesh->mName.C_Str(), totalVertices);

		modelMesh->_vertices.reserve(totalVertices);

		for (auto i = 0U; i < mesh->mNumFaces; ++i)
		{
			totalIndices += mesh->mFaces[i].mNumIndices;
		}

		// calculate transformation
		auto transformation = node->mTransformation;

		auto parent = node->mParent;
		while (parent)
		{
			transformation *= parent->mTransformation;
			parent = parent->mParent;
		}

		math::Matrix transmat = *(math::Matrix*)&transformation.a1;
		transmat = transmat.Transpose();

		std::vector<math::Vector4> points;

		if (totalIndices > 0)
			modelMesh->_indices.reserve(totalIndices);

		for (auto i = 0U; i < mesh->mNumVertices; ++i)
		{
			AnimatedMeshVertex vertex;

			vertex._position = AI2VEC4(mesh->mVertices[i]);
			//vertex._position = math::Vector4::Transform(vertex._position, transmat);

			points.push_back(vertex._position);

			if (mesh->mTextureCoords[0])
			{
				vertex._texcoord = AI2VEC2(mesh->mTextureCoords[0][i]);
			}

			// set up the tangent and bitangents
			if (mesh->mTangents != nullptr)
			{
				vertex._tangent = AI2VEC3(mesh->mTangents[i]);
				vertex._bitangent = AI2VEC3(mesh->mBitangents[i]);

				//vertex._tangent = math::Vector3::Transform(vertex._tangent, transmat);
				//vertex._bitangent = math::Vector3::Transform(vertex._bitangent, transmat);
			}

			if (mesh->mNormals != nullptr)
			{
				vertex._normal = AI2VEC3(mesh->mNormals[i]);
				//vertex._normal = math::Vector3::Transform(vertex._normal, transmat);
			}


			modelMesh->AddVertex(vertex);
		}

		// Construct the bounding boxes
		dx::BoundingBox::CreateFromPoints(modelMesh->_aabb, points.size(), (const math::Vector3*)points.data(), sizeof(math::Vector4));
		dx::BoundingOrientedBox::CreateFromBoundingBox(modelMesh->_obb, modelMesh->_aabb);

		// Load the bones
		for (uint32_t i = 0; i < mesh->mNumBones; i++)
		{
			uint32_t BoneIndex = 0;
			std::string BoneName(mesh->mBones[i]->mName.data);

			auto it = modelMesh->_boneMap.find(BoneName);

			if (it == modelMesh->_boneMap.end()) {
				// Allocate an index for a new bone
				BoneIndex = modelMesh->_boneMap.size();

				BoneInfo bi;
				bi.BoneOffset = math::Matrix(&mesh->mBones[i]->mOffsetMatrix.a1);
				modelMesh->_boneInfo.push_back(bi);

				modelMesh->_boneMap[BoneName] = BoneIndex;
			}
			else {
				BoneIndex = it->second;
			}

			for (uint32_t j = 0; j < mesh->mBones[i]->mNumWeights; j++)
			{
				uint32_t VertexID = mesh->mBones[i]->mWeights[j].mVertexId;
				float Weight = mesh->mBones[i]->mWeights[j].mWeight;

				modelMesh->_vertices[VertexID].AddBoneData(BoneIndex, Weight);
			}
		}



		for (uint32_t i = 0; i < mesh->mNumFaces; ++i)
		{
			aiFace& face = mesh->mFaces[i];

			for (uint32_t j = 0; j < face.mNumIndices; ++j)
			{
				modelMesh->_indices.push_back((uint32_t)face.mIndices[j]);
			}
		}

		if (mesh->mMaterialIndex >= 0)
		{
			auto material = scene->mMaterials[mesh->mMaterialIndex];

			if (material)
			{
				//modelMesh->_materialName = material->GetName().C_Str();
				//ProcessMaterial(modelMesh, scene, material);
			}
		}

		return modelMesh;
	}

#if 1
	void AssimpModelImporter::ProcessMaterial(Mesh* mesh, const aiScene* scene, aiMaterial* material, FileSystem* fileSystem)
	{		
		std::string matName = material->GetName().C_Str();		

		std::replace(matName.begin(), matName.end(), ':', '_');
		std::replace(matName.begin(), matName.end(), '.', '_');

		matName.append(".hmat");

		auto existingMaterial = g_pEnv->_materialLoader->FindMaterialByName(matName);

		if (existingMaterial)
		{
			mesh->SetMaterial(existingMaterial);
			return;
		}

		Material* mat = new Material;
		
		mat->SetPaths(_currentPath.parent_path() / matName, fileSystem);
		mat->SetName(matName);
		mat->SetLoader(g_pEnv->_materialLoader);
		mesh->SetMaterial(mat);

		MaterialProperties& props = mat->_properties;

		for (aiTextureType i = (aiTextureType)0; i < aiTextureType_UNKNOWN; i = (aiTextureType)((int)i + 1))
		{
			if (auto count = material->GetTextureCount(i); count > 0)
			{
				aiString str;
				material->GetTexture(i, 0, &str);

				LOG_DEBUG("Material has a texture type %d: %s", i, str.C_Str())
			}
		}

		// Set up some standard shaders
		mat->SetStandardShader(IShader::Create("EngineData.Shaders/Default.hcs"));
		mat->SetShadowMapShader(IShader::Create("EngineData.Shaders/ShadowMapGeometry.hcs"));

		if (material->GetTextureCount(aiTextureType_DIFFUSE) > 0)
		{
			mat->SetTexture(MaterialTexture::Albedo, LoadTexture(mesh, aiTextureType_DIFFUSE, scene, material, fileSystem));
		}
		else
		{
			mat->SetTexture(MaterialTexture::Albedo, ITexture2D::GetDefaultTexture());
		}

		if (material->GetTextureCount(aiTextureType_HEIGHT) > 0)
		{
			mat->SetTexture(MaterialTexture::Normal, LoadTexture(mesh, aiTextureType_HEIGHT, scene, material, fileSystem));
		}
		else if (material->GetTextureCount(aiTextureType_NORMALS) > 0)
		{
			mat->SetTexture(MaterialTexture::Normal, LoadTexture(mesh, aiTextureType_NORMALS, scene, material, fileSystem));
		}

		if (material->GetTextureCount(aiTextureType_SPECULAR) > 0)
		{
			mat->SetTexture(MaterialTexture::Roughness, LoadTexture(mesh, aiTextureType_SPECULAR, scene, material, fileSystem));
		}
		else if (material->GetTextureCount(aiTextureType_SHININESS) > 0)
		{
			mat->SetTexture(MaterialTexture::Roughness, LoadTexture(mesh, aiTextureType_SHININESS, scene, material, fileSystem));
		}

		if (material->GetTextureCount(aiTextureType_DISPLACEMENT) > 0)
		{
			mat->SetTexture(MaterialTexture::Height, LoadTexture(mesh, aiTextureType_DISPLACEMENT, scene, material, fileSystem));
		}

		if (material->GetTextureCount(aiTextureType_EMISSIVE) > 0)
		{
			mat->SetTexture(MaterialTexture::Emission, LoadTexture(mesh, aiTextureType_EMISSIVE, scene, material, fileSystem));
		}

		if (material->GetTextureCount(aiTextureType_AMBIENT_OCCLUSION) > 0)
		{
			mat->SetTexture(MaterialTexture::AmbientOcclusion, LoadTexture(mesh, aiTextureType_AMBIENT_OCCLUSION, scene, material, fileSystem));
		}
		

		// aiTextureType_DIFFUSE_ROUGHNESS

		float opacity = 1.0f;
		material->Get(AI_MATKEY_OPACITY, opacity);

		float transparency = 0.0f;
		material->Get(AI_MATKEY_TRANSPARENCYFACTOR, transparency);

		/*auto opacityMask = LoadTexture(mesh, aiTextureType_OPACITY, scene, material); opacityMask != nullptr*/
		if (material->GetTextureCount(aiTextureType_OPACITY) > 0 || opacity < 1.0f/* || transparency > 0.0f*/)
		{
			LOG_DEBUG("Material '%s' has transparency %.3f %.3f", material->GetName().C_Str(), opacity, transparency);
			mat->SetTexture(MaterialTexture::Opacity, LoadTexture(mesh, aiTextureType_OPACITY, scene, material, fileSystem));
			//props.hasTransparency = 1;
		}		

		aiColor3D emissiveColour;
		material->Get(AI_MATKEY_COLOR_EMISSIVE, emissiveColour);

		//material->Get(AI_MATKEY_SHININESS, props.shininess);

		//if (props.shininess == 0.0f)
		//	props.shininess = 1.0f;

		//aiColor3D specularColour;
		//material->Get(AI_MATKEY_COLOR_SPECULAR, specularColour);

		//props.specularColour = math::Vector4(specularColour.r, specularColour.g, specularColour.b, 1.0f);
		props.emissiveColour = math::Vector4(emissiveColour.r, emissiveColour.g, emissiveColour.b, emissiveColour.IsBlack() ? 0.0f : 1.0f);


		//float shininessStrength;
		//material->Get(AI_MATKEY_SHININESS_STRENGTH, props.shininessStrength);

		aiColor3D diffuseColour;
		material->Get(AI_MATKEY_COLOR_DIFFUSE, diffuseColour);

		// only set a diffuse colour if it has no texture, otherwise it will alter the appearance of the texture
		//if(auto tex = newMaterial->GetTexture(MaterialTexture::Diffuse); tex == nullptr || tex == ITexture2D::GetDefaultTexture())
		//	props.diffuseColour = math::Vector4(diffuseColour.r, diffuseColour.g, diffuseColour.b, opacity);
		
		//LOG_DEBUG("Material '%s' shininess = %f, shininessStrength = %f", material->GetName().C_Str(), props.shininess, props.shininessStrength);

		//aiGetMaterialColor(material, AI_MATKEY_COLOR_EMISSIVE, 
		for (int i = 0; i < material->mNumProperties; ++i)
		{
			LOG_DEBUG("Material '%s' has property '%s' with type %d",
				material->GetName().C_Str(), material->mProperties[i]->mKey.C_Str(), material->mProperties[i]->mType);
		}

		mat->Save();
	}
#endif

	ITexture2D* AssimpModelImporter::LoadTexture(Mesh* mesh, const aiTextureType type, const aiScene* scene, const aiMaterial* material, FileSystem* fileSystem)
	{
		auto GetTextureType = [](const aiScene* scene, const aiString& str) {

			std::string textypeteststr = str.C_Str();

			if (textypeteststr == "*0" || textypeteststr == "*1" || textypeteststr == "*2" || textypeteststr == "*3" || textypeteststr == "*4" || textypeteststr == "*5")
			{
				if (scene->mTextures[0]->mHeight == 0)
				{
					return 1; // embedded compressed
				}
				else
				{
					return 2; // embedded non-compressed
				}
			}
			if (textypeteststr.find('.') != std::string::npos)
			{
				return 0;
			}

			return 0;
		};

		int textureType = -1;

		if (auto count = material->GetTextureCount(type); count > 0)
		{
			aiString str;
			material->GetTexture(type, 0, &str);

			std::string path = str.C_Str();

			LOG_DEBUG("Texture '%s' has %d textures!", path.c_str(), count);

			if (textureType == -1)
				textureType = GetTextureType(scene, str);

			if (path[0] == '/')
				path.erase(0, 1);

			fs::path relativePath;
			fs::path fsPath;

			if (_importOpts.textureSearchPath.length() > 0)
			{
				relativePath = _importOpts.textureSearchPath;
				relativePath /= fs::path(path).filename();
			}
			else
			{
				relativePath = mesh->GetRelativePath().parent_path();
				relativePath /= path;			
			}

			if (_importOpts.replaceTextureExtension.length() > 0)
			{
				relativePath.replace_extension(_importOpts.replaceTextureExtension);
			}

			fsPath = fileSystem->GetRelativeResourcePath(relativePath);

			auto texture = (ITexture2D*)g_pEnv->_resourceSystem->LoadResource(fsPath);

			if (!texture)
			{
				LOG_WARN("*** Mesh texture load failed for: %s", relativePath.string().c_str());
			}

			return texture;
		}

		return nullptr;
	}

	// IOSystem
	bool AssimpIoSystem::Exists(const char* pFile) const
	{		
		return g_pEnv->_resourceSystem->DoesResourceExistAsAsset(pFile);
	}

	AssimpIoStream* AssimpIoSystem::Open(const char* pFile, const char* pMode)
	{
		return new AssimpIoStream(pFile, pMode);
	}

	void AssimpIoSystem::Close(Assimp::IOStream* pFile)
	{
		delete pFile;
	}

	// IOStream
	AssimpIoStream::AssimpIoStream(const char* pFile, const char* pMode)
	{
		_path = pFile;
		_readOffset = 0;

		FileSystem* assetSystem = g_pEnv->_resourceSystem->FindAssetFileSystemForAsset(_path);

		if (!assetSystem)
			return;

		assetSystem->GetFileData(_path, _fileData);
	}

	size_t AssimpIoStream::Read(void* pvBuffer, size_t pSize, size_t pCount)
	{
		if (_readOffset >= _fileData.size())
			return 0;

		memcpy(pvBuffer, _fileData.data() + _readOffset, pCount * pSize);

		return pCount * pSize;
	}

	size_t AssimpIoStream::Write(const void* pvBuffer, size_t pSize, size_t pCount)
	{
		return 0;
	}

	aiReturn AssimpIoStream::Seek(size_t pOffset, aiOrigin pOrigin)
	{
		switch (pOrigin)
		{
		case aiOrigin::aiOrigin_SET:
			_readOffset = pOffset;
			break;

		case aiOrigin::aiOrigin_CUR:
			_readOffset += pOffset;
			break;

		case aiOrigin::aiOrigin_END:
			_readOffset = _fileData.size();
			break;
		}

		if (_readOffset >= _fileData.size())
			return aiReturn::aiReturn_OUTOFMEMORY;

		return aiReturn::aiReturn_SUCCESS;
	}

	size_t AssimpIoStream::Tell() const
	{
		return _readOffset;
	}

	size_t AssimpIoStream::FileSize() const
	{
		return _fileData.size();
	}

	void AssimpIoStream::Flush()
	{

	}
}