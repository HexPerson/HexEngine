

#include "AssimpModelSystem.hpp"
#include <HexEngine.Core\Graphics\MaterialLoader.hpp>

#define AI2VEC2(val) math::Vector2(val.x, val.y);
#define AI2VEC3(val) math::Vector3(val.x, val.y, val.z);
#define AI2VEC4(val) math::Vector4(val.x, val.y, val.z, 1.0f);

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
//aiProcess_SortByPType | // ?
aiProcess_ImproveCacheLocality | // improve the cache locality of the output vertices
//aiProcess_RemoveRedundantMaterials | // remove redundant materials
//aiProcess_FindDegenerates | // remove degenerated polygons from the import
//aiProcess_FindInvalidData | // detect invalid model data, such as invalid normal vectors
aiProcess_GenUVCoords | // convert spherical, cylindrical, box and planar mapping to proper UVs
aiProcess_TransformUVCoords | // preprocess UV transformations (scaling, translation ...)
//aiProcess_FindInstances | // search for instanced meshes and remove them by references to one master
aiProcess_LimitBoneWeights | // limit bone weights to 4 per vertex
aiProcess_OptimizeMeshes | // join small meshes, if possible;

//aiProcess_SplitLargeMeshes |
//aiProcess_PreTransformVertices | // this will drop animations !!

//aiProcess_SplitByBoneCount | // split meshes with too many bones. Necessary for our (limited) hardware skinning shader
//aiProcess_MakeLeftHanded |
0
#endif
;

AssimpModelImporter::AssimpModelImporter()
{
	HexEngine::g_pEnv->GetResourceSystem().RegisterResourceLoader(this);
}

AssimpModelImporter::~AssimpModelImporter()
{
	HexEngine::g_pEnv->GetResourceSystem().UnregisterResourceLoader(this);
}

void AssimpModelImporter::Destroy()
{
}

#include <shlobj.h>

static int32_t CALLBACK BrowseCallbackProc(HWND hwnd, UINT uMsg, LPARAM lParam, LPARAM lpData)
{
	if (uMsg == BFFM_INITIALIZED)
	{
		SendMessage(hwnd, BFFM_SETSELECTION, TRUE, lpData);
	}

	return 0;
};

bool AssimpModelImporter::OnBrowseFolderPath(HexEngine::LineEdit* edit)
{
	wchar_t baseDirectory[MAX_PATH];
	wcscpy_s(baseDirectory, HexEngine::g_pEnv->GetFileSystem().GetBaseDirectory().wstring().c_str());

	BROWSEINFO bi = { 0 };
	bi.lpszTitle = L"Browse for folder...";
	bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
	bi.lpfn = BrowseCallbackProc;
	bi.lParam = (LPARAM)baseDirectory;

	LPITEMIDLIST pidl = SHBrowseForFolder(&bi);

	std::wstring path;

	if (pidl != 0)
	{
		wchar_t tempPath[MAX_PATH];
		//get the name of the folder and put it in path
		SHGetPathFromIDList(pidl, tempPath);

		//free memory used
		IMalloc* imalloc = 0;
		if (SUCCEEDED(SHGetMalloc(&imalloc)))
		{
			imalloc->Free(pidl);
			imalloc->Release();
		}

		path = tempPath;
	}
	else
		return true;

	if (path.length() == 0)
	{
		return true;
	}

	edit->SetValue(path);
	return true;
}

HexEngine::Dialog* AssimpModelImporter::CreateEditorDialog(const std::vector<fs::path>& paths)
{
	const int32_t width = 800;
	const int32_t height = 600;

	HexEngine::Dialog* dlg = new HexEngine::Dialog(
		HexEngine::g_pEnv->GetUIManager().GetRootElement(),
		HexEngine::Point::GetScreenCenterWithOffset(-width / 2, -height / 2),
		HexEngine::Point(width, height),
		paths.size() == 1 ? std::format(L"Importing model: {}", paths[0].filename().wstring()) : std::format(L"Importing {} models", paths.size()));

	dlg->BringToFront();

	auto layout = new HexEngine::ComponentWidget(dlg,
		HexEngine::Point(10, 10),
		HexEngine::Point(dlg->GetSize().x - 20, dlg->GetSize().y - 20),
		L"");

	auto importAnims = new HexEngine::Checkbox(layout, layout->GetNextPos(), HexEngine::Point(200, 20), L"Import animations", &_importOpts.importAnimations);
	auto createAnims = new HexEngine::Checkbox(layout, layout->GetNextPos(), HexEngine::Point(200, 20), L"Create materials", &_importOpts.tryAndCreateMaterials);
	auto renameFiles = new HexEngine::Checkbox(layout, layout->GetNextPos(), HexEngine::Point(200, 20), L"Rename files", &_importOpts.renameFiles);
	auto deleteOriginals = new HexEngine::Checkbox(layout, layout->GetNextPos(), HexEngine::Point(200, 20), L"Delete originals after import", &_importOpts.deleteOriginalsAfterImport);

	constexpr int32_t kTexturePathArrayHeight = 240;
	auto array = new HexEngine::ArrayElement<std::wstring>(
		layout,
		layout->GetNextPos(),
		HexEngine::Point(640, kTexturePathArrayHeight),
		L"Texture search paths",
		_importOpts.textureSearchPaths,
		[this](HexEngine::Element* rowRoot, std::wstring& item, int32_t index)
		{
			constexpr int32_t kBrowseButtonWidth = 120;
			constexpr int32_t kSpacing = 6;
			constexpr int32_t kRowControlHeight = 24;
			const int32_t editWidth = std::max(100, rowRoot->GetSize().x - kBrowseButtonWidth - kSpacing);

			auto* pathEdit = new HexEngine::LineEdit(rowRoot, HexEngine::Point(0, 0), HexEngine::Point(editWidth, kRowControlHeight), L"");
			pathEdit->SetDoesCallbackWaitForReturn(false);
			pathEdit->SetValue(item);
			pathEdit->SetOnInputFn([this, index](HexEngine::LineEdit*, const std::wstring& text)
			{
				if (index < 0 || index >= (int32_t)_importOpts.textureSearchPaths.size())
					return;

				_importOpts.textureSearchPaths[index] = text;
			});

			new HexEngine::Button(
				rowRoot,
				HexEngine::Point(editWidth + kSpacing, -1),
				HexEngine::Point(kBrowseButtonWidth, 26),
				L"Browse...",
				[this, pathEdit, index](HexEngine::Button*) -> bool
				{
					if (!OnBrowseFolderPath(pathEdit))
						return false;

					if (index >= 0 && index < (int32_t)_importOpts.textureSearchPaths.size())
					{
						_importOpts.textureSearchPaths[index] = pathEdit->GetValue();
					}

					return true;
				});
		},
		[]() -> std::wstring { return std::wstring(); },
		[](const std::wstring&, int32_t) -> int32_t { return 48; },
		[](const std::wstring&, int32_t index) -> std::wstring
		{
			return L"Path " + std::to_wstring(index + 1);
		});

	auto textureExtension = new HexEngine::LineEdit(layout, layout->GetNextPos(), HexEngine::Point(500, 20), L"Override texture extension");
	textureExtension->SetDoesCallbackWaitForReturn(false);
	textureExtension->SetValue(_importOpts.replaceTextureExtension);
	textureExtension->SetOnInputFn([this](HexEngine::LineEdit*, const std::wstring& text) {
		_importOpts.replaceTextureExtension = text;
		});

	auto texturePrefix = new HexEngine::LineEdit(layout, layout->GetNextPos(), HexEngine::Point(500, 20), L"Texture prefix");
	texturePrefix->SetDoesCallbackWaitForReturn(false);
	texturePrefix->SetValue(_importOpts.texturePrefix);
	texturePrefix->SetOnInputFn([this](HexEngine::LineEdit*, const std::wstring& text) {
		_importOpts.texturePrefix = text;
		});

	auto importModel = [&](std::vector<fs::path> pathsToSearch) -> bool {

		for (auto& path : pathsToSearch)
		{
			auto fileSystem = HexEngine::g_pEnv->GetResourceSystem().FindFileSystemByPath(path);
			auto fullPath = fileSystem->GetLocalAbsoluteDataPath(path);
			LoadResourceFromFile(fullPath, fileSystem, nullptr);
		}


		return true;
		};

	auto import = new HexEngine::Button(layout, layout->GetNextPos(), HexEngine::Point(80, 20), L"Import",
		std::bind(importModel, paths)
	/*std::bind(&AssimpModelImporter::LoadResourceFromFile, this, fileSystem->GetLocalAbsoluteDataPath(path), fileSystem, nullptr)*/);

	return dlg;
}



std::shared_ptr<HexEngine::IResource> AssimpModelImporter::LoadResourceFromFile(const fs::path& path, HexEngine::FileSystem* fileSystem, const HexEngine::ResourceLoadOptions* options /*= nullptr*/)
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

	_createdMeshes.clear();

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



		aiVector3D upVec = upAxis == 0 ? aiVector3D((ai_real)upAxisSign, 0, 0) : upAxis == 1 ? aiVector3D(0, (ai_real)upAxisSign, 0) : aiVector3D(0, 0, (ai_real)upAxisSign);
		aiVector3D forwardVec = frontAxis == 0 ? aiVector3D((ai_real)frontAxisSign, 0, 0) : frontAxis == 1 ? aiVector3D(0, (ai_real)frontAxisSign, 0) : aiVector3D(0, 0, (ai_real)frontAxisSign);
		aiVector3D rightVec = coordAxis == 0 ? aiVector3D((ai_real)coordAxisSign, 0, 0) : coordAxis == 1 ? aiVector3D(0, (ai_real)coordAxisSign, 0) : aiVector3D(0, 0, (ai_real)coordAxisSign);
		aiMatrix4x4 mat(rightVec.x, rightVec.y, rightVec.z, 0.0f,
			upVec.x, upVec.y, upVec.z, 0.0f,
			forwardVec.x, forwardVec.y, forwardVec.z, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f);

		//modelScene->mRootNode->mTransformation *= mat;
	}

	std::shared_ptr<HexEngine::Model> model = std::shared_ptr<HexEngine::Model>(new HexEngine::Model, HexEngine::ResourceDeleter());

	if (_importOpts.importAnimations)
		ProcessAnimations(model, modelScene);

	std::vector<HexEngine::AnimChannel*> parents(modelScene->mNumAnimations);

	ProcessNode(model, modelScene->mRootNode, parents, modelScene, fileSystem);

	for (auto& mesh : _createdMeshes)
	{
		mesh.second->Save();

		// rename the file back to .hmesh
		fs::path renamedFile = mesh.first;
		fs::rename(mesh.first, renamedFile.replace_extension(".hmesh"));
	}

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

	if (_importOpts.deleteOriginalsAfterImport)
	{
		//fs::remove(path);
	}

	return model;
}

std::shared_ptr<HexEngine::IResource> AssimpModelImporter::LoadResourceFromMemory(const std::vector<uint8_t>& data, const fs::path& relativePath, HexEngine::FileSystem* fileSystem, const HexEngine::ResourceLoadOptions* options)
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

	std::shared_ptr<HexEngine::Model> model = std::shared_ptr<HexEngine::Model>(new HexEngine::Model, HexEngine::ResourceDeleter());
	model->SetPaths(relativePath, fileSystem);

	_createdMeshes.clear();

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

	std::vector<HexEngine::AnimChannel*> parentAnims(modelScene->mNumAnimations);

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

	for (auto& mesh : _createdMeshes)
	{
		mesh.second->Save();

		// rename the file back to .hmesh
		fs::path renamedFile = mesh.first;
		fs::rename(mesh.first, renamedFile.replace_extension(".hmesh"));
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

void AssimpModelImporter::ProcessAnimations(std::shared_ptr<HexEngine::Model>& model, const aiScene* scene)
{
	if (!scene->mNumAnimations)
		return;

	auto animData = std::make_shared<HexEngine::AnimationData>();	

	animData->_globalInverseTransform = math::Matrix(&scene->mRootNode->mTransformation.a1);
	//model->_animData->_globalInverseTransform = model->_animData->_globalInverseTransform.Invert(); // do we need this?


	for (auto i = 0u; i < scene->mNumAnimations; ++i)
	{
		auto animation = scene->mAnimations[i];

		if (!animation)
			continue;

		HexEngine::Animation anim;
		anim.name = animation->mName.data;
		anim.duration = (float)animation->mDuration;
		anim.ticksPerSecond = (float)animation->mTicksPerSecond;

		for (uint32_t j = 0; j < animation->mNumChannels; ++j)
		{
			HexEngine::AnimChannel chan;
			chan.nodeName = animation->mChannels[j]->mNodeName.data;

			for (uint32_t p = 0; p < animation->mChannels[j]->mNumPositionKeys; ++p)
			{
				auto& key = animation->mChannels[j]->mPositionKeys[p];
				chan.positionKeys.push_back({ key.mTime, math::Vector3(key.mValue.x, key.mValue.y, key.mValue.z) });
			}

			for (uint32_t p = 0; p < animation->mChannels[j]->mNumRotationKeys; ++p)
			{
				auto& key = animation->mChannels[j]->mRotationKeys[p];
				chan.rotationKeys.push_back({ key.mTime, math::Quaternion(key.mValue.x, key.mValue.y, key.mValue.z, key.mValue.w) });
			}

			for (uint32_t p = 0; p < animation->mChannels[j]->mNumScalingKeys; ++p)
			{
				auto& key = animation->mChannels[j]->mScalingKeys[p];
				chan.scaleKeys.push_back({ key.mTime, math::Vector3(key.mValue.x, key.mValue.y, key.mValue.z) });
			}

			anim.channels.push_back(chan);
		}

		animData->_animations.push_back(anim);
	}

	model->SetAnimatioData(animData);
}

std::vector<std::string> AssimpModelImporter::GetSupportedResourceExtensions()
{
	return { ".obj", ".fbx", ".blend", ".mtl", ".glb", ".gltf", ".max" };
}

std::wstring AssimpModelImporter::GetResourceDirectory() const
{
	return L"Models";
}

void AssimpModelImporter::UnloadResource(HexEngine::IResource* resource)
{
	SAFE_DELETE(resource);
}

void AssimpModelImporter::ProcessNode(std::shared_ptr<HexEngine::Model>& model, aiNode* node, std::vector<HexEngine::AnimChannel*> parentAnims, const aiScene* scene, HexEngine::FileSystem* fileSystem)
{
	if (model->GetAnimationData() && _importOpts.importAnimations)
	{
		int32_t animIdx = 0;

		for (auto& anim : model->GetAnimationData()->_animations)
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

		if (scene->HasAnimations())
			ProcessAnimatedMesh(model, mesh, scene, node, fileSystem);
		else
			ProcessMesh(model, mesh, scene, node, fileSystem);
	}

	for (uint32_t i = 0; i < node->mNumChildren; ++i)
	{
		ProcessNode(model, node->mChildren[i], parentAnims, scene, fileSystem);
	}
}

HexEngine::AnimChannel* AssimpModelImporter::FindAnimChannelFromNodeName(std::shared_ptr<HexEngine::Model>& model, const std::string& nodeName)
{
	for (auto& anim : model->GetAnimationData()->_animations)
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

std::shared_ptr<HexEngine::Mesh> AssimpModelImporter::ProcessMesh(std::shared_ptr<HexEngine::Model>& model, aiMesh* mesh, const aiScene* scene, aiNode* node, HexEngine::FileSystem* fileSystem)
{
	std::string meshName = mesh->mName.C_Str();

	meshName.erase(std::remove(meshName.begin(), meshName.end(), ':'));

	auto findStringToChar = [](std::string s, char c)
		{

		};

	// Fix for bad mesh names e.g. "Paris_Street_4.Paris_Street_4.Paris_Street_4.Paris_Street_4.Paris_Street_4.Paris_Street_4.Paris_Street_4.Paris_Street_4.Paris_Street_4" etc
	while (true)
	{
		if (auto p = meshName.find('.'); p != meshName.npos)
		{
			std::string first = meshName.substr(0, p);
			std::string second = meshName.substr(p + 1);

			auto p2 = second.find('.');

			if (p2 != meshName.npos)
			{
				auto secondSplit = second.substr(0, p2);

				if (secondSplit == first)
				{
					meshName.erase(0, p+1);
				}
				else
					break;
			}
			else
			{
				meshName = first;
				break;
			}
		}
		else
			break;
	}
		
	meshName.erase(std::remove(meshName.begin(), meshName.end(), '.'));

	// if the mesh name is the same as the model name, try and use the material as the unique name instead
	if (_currentPath.stem().string() == meshName)
	{
		auto material = scene->mMaterials[mesh->mMaterialIndex];

		if (material)
		{
			meshName = material->GetName().C_Str();
		}
	}

	/*if (_importOpts.renameFiles)
	{
		meshName.append("_");
		meshName.append(std::to_string(mesh->mNumVertices));
		meshName.append("_");
		meshName.append(std::to_string(mesh->mNumFaces));
	}*/

	// Pre-calculate needed vertices and indices
	//
	std::shared_ptr<HexEngine::Mesh> modelMesh = std::shared_ptr<HexEngine::Mesh>(new HexEngine::Mesh(model, meshName), HexEngine::ResourceDeleter());

	auto fixedExtensionPath = _currentPath;

	if (_importOpts.renameFiles == false)
		fixedExtensionPath.replace_filename(_currentPath.stem().string());
	else
	{
		auto currentFileName = _currentPath.stem().filename().string();

		std::transform(meshName.begin(), meshName.end(), meshName.begin(), ::tolower);
		std::transform(currentFileName.begin(), currentFileName.end(), currentFileName.begin(), ::tolower);
		if (meshName != currentFileName)
		{
			fixedExtensionPath.replace_filename(_currentPath.stem().string() + "_" + meshName);
		}
		else
		{
			bool a = false;
		}
	}

	// we give it a temporary file extension to stop the file change notifier picking it up whilst its still being written to
	fixedExtensionPath.replace_extension(".hmesh_tmp");

	//modelMesh->AddRef();
	modelMesh->SetPaths(fixedExtensionPath, fileSystem);
	modelMesh->SetLoader(HexEngine::g_pEnv->_meshLoader);
	modelMesh->SetNumFaces(mesh->mNumFaces);

	uint32_t totalIndices = 0;
	uint32_t totalVertices = mesh->mNumVertices;

	model->AddMesh(modelMesh);

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

	for (auto i = 0U; i < mesh->mNumVertices; ++i)
	{
		HexEngine::MeshVertex vertex;

		vertex._position = AI2VEC4(mesh->mVertices[i]);

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
		}

		if (mesh->mNormals != nullptr)
		{
			vertex._normal = AI2VEC3(mesh->mNormals[i]);
		}

		modelMesh->AddVertex(vertex);
	}

	for (uint32_t i = 0; i < mesh->mNumFaces; ++i)
	{
		aiFace& face = mesh->mFaces[i];

		for (uint32_t j = 0; j < face.mNumIndices; ++j)
		{
			const auto index = face.mIndices[j];

			modelMesh->AddIndex((HexEngine::MeshIndexFormat)index);
		}
	}

	// Construct the bounding boxes
	dx::BoundingBox abb;
	dx::BoundingBox::CreateFromPoints(abb, points.size(), (const math::Vector3*)points.data(), sizeof(math::Vector4));
	modelMesh->SetAABB(abb);

	dx::BoundingOrientedBox obb;
	dx::BoundingOrientedBox::CreateFromBoundingBox(obb, modelMesh->GetAABB());
	modelMesh->SetOBB(obb);

	if (_importOpts.tryAndCreateMaterials && mesh->mMaterialIndex >= 0)
	{
		auto material = scene->mMaterials[mesh->mMaterialIndex];

		if (material)
		{
			modelMesh->SetMaterialName(material->GetName().C_Str());
			ProcessMaterial(modelMesh, scene, material, fileSystem);
		}
	}

	_createdMeshes.push_back({ fixedExtensionPath , modelMesh });

	//modelMesh->Save();

	//_createdMeshes.push_back({ fixedExtensionPath , modelMesh });

	//// rename the file back to .hmesh
	//fs::path renamedFile = fixedExtensionPath;
	//fs::rename(fixedExtensionPath, renamedFile.replace_extension(".hmesh"));

	return modelMesh;
}

#if 1
std::shared_ptr<HexEngine::AnimatedMesh> AssimpModelImporter::ProcessAnimatedMesh(std::shared_ptr<HexEngine::Model>& model, aiMesh* mesh, const aiScene* scene, aiNode* node, HexEngine::FileSystem* fileSystem)
{
	std::string meshName = mesh->mName.C_Str();

	meshName.erase(std::remove(meshName.begin(), meshName.end(), ':'));
	meshName.erase(std::remove(meshName.begin(), meshName.end(), '.'));

	/*if (_importOpts.renameFiles)
	{
		meshName.append("_");
		meshName.append(std::to_string(mesh->mNumVertices));
		meshName.append("_");
		meshName.append(std::to_string(mesh->mNumFaces));
	}*/

	// Pre-calculate needed vertices and indices
	//
	std::shared_ptr<HexEngine::AnimatedMesh> modelMesh = std::shared_ptr<HexEngine::AnimatedMesh>(new HexEngine::AnimatedMesh(model, meshName), HexEngine::ResourceDeleter());

	auto fixedExtensionPath = _currentPath;

	// set the animation data
	modelMesh->SetAnimationData(model->GetAnimationData());

	if (_importOpts.renameFiles == false)
		fixedExtensionPath.replace_filename(_currentPath.stem().string());
	else
		fixedExtensionPath.replace_filename(_currentPath.stem().string() + "_" + meshName);

	fixedExtensionPath.replace_extension(".hmesh_tmp");

	//modelMesh->AddRef();
	modelMesh->SetPaths(fixedExtensionPath, fileSystem);
	modelMesh->SetLoader(HexEngine::g_pEnv->_meshLoader);
	modelMesh->SetNumFaces(mesh->mNumFaces);

	// set the root transformation for this node
	modelMesh->SetRootTransformation(math::Matrix(&node->mTransformation.a1));

	uint32_t totalIndices = 0;
	uint32_t totalVertices = mesh->mNumVertices;

	LOG_DEBUG("Mesh '%s' has %d vertices", mesh->mName.C_Str(), totalVertices);

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

	for (auto i = 0U; i < mesh->mNumVertices; ++i)
	{
		HexEngine::AnimatedMeshVertex vertex;

		vertex._position = AI2VEC4(mesh->mVertices[i]);

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
		}

		if (mesh->mNormals != nullptr)
		{
			vertex._normal = AI2VEC3(mesh->mNormals[i]);
		}


		modelMesh->AddVertex(vertex);
	}

	// Construct the bounding boxes
	dx::BoundingBox abb;
	dx::BoundingBox::CreateFromPoints(abb, points.size(), (const math::Vector3*)points.data(), sizeof(math::Vector4));
	modelMesh->SetAABB(abb);

	dx::BoundingOrientedBox obb;
	dx::BoundingOrientedBox::CreateFromBoundingBox(obb, abb);
	modelMesh->SetOBB(obb);

	HexEngine::AnimatedMesh::BoneNameMap boneMap;
	HexEngine::AnimatedMesh::BoneInfoArray boneInfo;

	auto& vertices = modelMesh->GetVertices();

	// Load the bones
	for (uint32_t i = 0; i < mesh->mNumBones; i++)
	{
		uint32_t BoneIndex = 0;
		std::string BoneName(mesh->mBones[i]->mName.data);

		auto it = boneMap.find(BoneName);

		if (it == boneMap.end()) {
			// Allocate an index for a new bone
			BoneIndex = boneMap.size();

			HexEngine::BoneInfo bi;
			bi.BoneOffset = math::Matrix(&mesh->mBones[i]->mOffsetMatrix.a1);
			boneInfo[i] = bi;

			boneMap[BoneName] = BoneIndex;
		}
		else {
			BoneIndex = it->second;
		}

		for (uint32_t j = 0; j < mesh->mBones[i]->mNumWeights; j++)
		{
			uint32_t VertexID = mesh->mBones[i]->mWeights[j].mVertexId;
			float Weight = mesh->mBones[i]->mWeights[j].mWeight;

			vertices[VertexID].AddBoneData(BoneIndex, Weight);
		}
	}

	modelMesh->SetBoneMap(mesh->mNumBones, boneMap, boneInfo);

	for (uint32_t i = 0; i < mesh->mNumFaces; ++i)
	{
		aiFace& face = mesh->mFaces[i];

		for (uint32_t j = 0; j < face.mNumIndices; ++j)
		{
			modelMesh->AddIndex((uint32_t)face.mIndices[j]);
		}
	}

	if (_importOpts.tryAndCreateMaterials && mesh->mMaterialIndex >= 0)
	{
		auto material = scene->mMaterials[mesh->mMaterialIndex];

		if (material)
		{
			modelMesh->SetMaterialName(material->GetName().C_Str());
			ProcessMaterial(dynamic_pointer_cast<HexEngine::Mesh>(modelMesh), scene, material, fileSystem);
		}
	}

	_createdMeshes.push_back({ fixedExtensionPath , modelMesh });

	//modelMesh->Save();

	//_createdMeshes.push_back({ fixedExtensionPath , modelMesh });

	//// rename the file back to .hmesh
	//fs::path renamedFile = fixedExtensionPath;
	//fs::rename(fixedExtensionPath, renamedFile.replace_extension(".hmesh"));

	return modelMesh;
}
#endif

#if 1
void AssimpModelImporter::ProcessMaterial(std::shared_ptr<HexEngine::Mesh> mesh, const aiScene* scene, aiMaterial* material, HexEngine::FileSystem* fileSystem)
{
	std::string matName = material->GetName().C_Str();

	std::replace(matName.begin(), matName.end(), ':', '_');
	std::replace(matName.begin(), matName.end(), '.', '_');

	matName.append(".hmat");

	auto existingMaterial = HexEngine::g_pEnv->_materialLoader->FindMaterialByName(matName);

	if (existingMaterial)
	{
		mesh->SetMaterial(existingMaterial);
		return;
	}

	std::shared_ptr<HexEngine::Material> mat = std::shared_ptr<HexEngine::Material>(new HexEngine::Material);

	mat->SetPaths(_currentPath.parent_path() / matName, fileSystem);
	mat->SetName(matName);
	mat->SetLoader(HexEngine::g_pEnv->_materialLoader);
	mesh->SetMaterial(mat);

	HexEngine::MaterialProperties& props = mat->_properties;

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
	mat->SetStandardShader(HexEngine::IShader::Create("EngineData.Shaders/Default.hcs"));
	mat->SetShadowMapShader(HexEngine::IShader::Create("EngineData.Shaders/ShadowMapGeometry.hcs"));

	if (material->GetTextureCount(aiTextureType_DIFFUSE) > 0)
	{
		mat->SetTexture(HexEngine::MaterialTexture::Albedo, LoadTexture(mesh, aiTextureType_DIFFUSE, scene, material, fileSystem));
	}
	else
	{
		mat->SetTexture(HexEngine::MaterialTexture::Albedo, HexEngine::ITexture2D::GetDefaultTexture());
	}

	if (material->GetTextureCount(aiTextureType_HEIGHT) > 0)
	{
		mat->SetTexture(HexEngine::MaterialTexture::Normal, LoadTexture(mesh, aiTextureType_HEIGHT, scene, material, fileSystem));
	}
	else if (material->GetTextureCount(aiTextureType_NORMALS) > 0)
	{
		mat->SetTexture(HexEngine::MaterialTexture::Normal, LoadTexture(mesh, aiTextureType_NORMALS, scene, material, fileSystem));
	}

	if (material->GetTextureCount(aiTextureType_SPECULAR) > 0)
	{
		mat->SetTexture(HexEngine::MaterialTexture::Roughness, LoadTexture(mesh, aiTextureType_SPECULAR, scene, material, fileSystem));
	}
	else if (material->GetTextureCount(aiTextureType_SHININESS) > 0)
	{
		mat->SetTexture(HexEngine::MaterialTexture::Roughness, LoadTexture(mesh, aiTextureType_SHININESS, scene, material, fileSystem));
	}

	if (material->GetTextureCount(aiTextureType_DISPLACEMENT) > 0)
	{
		mat->SetTexture(HexEngine::MaterialTexture::Height, LoadTexture(mesh, aiTextureType_DISPLACEMENT, scene, material, fileSystem));
	}

	if (material->GetTextureCount(aiTextureType_EMISSIVE) > 0)
	{
		mat->SetTexture(HexEngine::MaterialTexture::Emission, LoadTexture(mesh, aiTextureType_EMISSIVE, scene, material, fileSystem));
	}

	if (material->GetTextureCount(aiTextureType_AMBIENT_OCCLUSION) > 0)
	{
		mat->SetTexture(HexEngine::MaterialTexture::AmbientOcclusion, LoadTexture(mesh, aiTextureType_AMBIENT_OCCLUSION, scene, material, fileSystem));
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
		mat->SetTexture(HexEngine::MaterialTexture::Opacity, LoadTexture(mesh, aiTextureType_OPACITY, scene, material, fileSystem));
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
	for (uint32_t i = 0; i < material->mNumProperties; ++i)
	{
		LOG_DEBUG("Material '%s' has property '%s' with type %d",
			material->GetName().C_Str(), material->mProperties[i]->mKey.C_Str(), material->mProperties[i]->mType);
	}

	mat->Save();
}
#endif

std::shared_ptr<HexEngine::ITexture2D> AssimpModelImporter::LoadTexture(std::shared_ptr<HexEngine::Mesh>& mesh, const aiTextureType type, const aiScene* scene, const aiMaterial* material, HexEngine::FileSystem* fileSystem)
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

		std::vector<fs::path> pathCandidates;
		pathCandidates.reserve(_importOpts.textureSearchPaths.size() + 1);

		fs::path textureFileName = fs::path(path).filename();

		if (_importOpts.texturePrefix.length() > 0)
		{
			// prepend the prefix if it doesn't already exist
			if(textureFileName.wstring().find(_importOpts.texturePrefix) != 0)
				textureFileName = fs::path(_importOpts.texturePrefix + textureFileName.wstring());
		}
		for (const auto& searchPath : _importOpts.textureSearchPaths)
		{
			if (searchPath.empty())
				continue;

			fs::path candidate = fs::path(searchPath) / textureFileName;
			if (_importOpts.replaceTextureExtension.length() > 0)
			{
				candidate.replace_extension(_importOpts.replaceTextureExtension);
			}

			pathCandidates.push_back(candidate);
		}

		fs::path defaultCandidate = mesh->GetRelativePath().parent_path() / fs::path(path);
		if (_importOpts.replaceTextureExtension.length() > 0)
		{
			defaultCandidate.replace_extension(_importOpts.replaceTextureExtension);
		}
		pathCandidates.push_back(defaultCandidate);

		fs::path selectedPath = pathCandidates.front();
		for (const auto& candidate : pathCandidates)
		{
			fs::path absoluteCandidate = candidate;
			if (!absoluteCandidate.is_absolute())
			{
				absoluteCandidate = fileSystem->GetLocalAbsoluteDataPath(candidate);
			}

			if (fileSystem->DoesAbsolutePathExist(absoluteCandidate))
			{
				selectedPath = candidate;
				break;
			}
		}

		fs::path fsPath = selectedPath;
		if (!selectedPath.is_absolute())
		{
			fsPath = fileSystem->GetRelativeResourcePath(selectedPath);
		}
		else
		{
			std::error_code relPathError;
			const fs::path relativeToData = fs::relative(selectedPath, fileSystem->GetDataDirectory(), relPathError);
			if (!relPathError && !relativeToData.empty() && relativeToData.native()[0] != '.')
			{
				fsPath = fileSystem->GetRelativeResourcePath(relativeToData);
			}
		}

		auto texture = HexEngine::ITexture2D::Create(fsPath);

		if (!texture)
		{
			LOG_WARN("*** Mesh texture load failed for: %s", selectedPath.string().c_str());
		}
		else
		{
			LOG_INFO("*** Mesh texture load failed for: %s", selectedPath.string().c_str());
		}

		return texture;
	}

	return nullptr;
}

// IOSystem
bool AssimpIoSystem::Exists(const char* pFile) const
{
	return HexEngine::g_pEnv->GetResourceSystem().DoesResourceExistAsAsset(pFile);
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

	HexEngine::FileSystem* assetSystem = HexEngine::g_pEnv->GetResourceSystem().FindAssetFileSystemForAsset(_path);

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
