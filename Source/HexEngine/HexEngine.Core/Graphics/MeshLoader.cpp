
#include "MeshLoader.hpp"
#include "../Environment/IEnvironment.hpp"
#include "../Environment/LogFile.hpp"
#include "../Scene/Mesh.hpp"
#include "../Scene/AnimatedMesh.hpp"
#include "../FileSystem/FileSystem.hpp"
#include "Material.hpp"

namespace HexEngine
{
	MeshLoader::MeshLoader()
	{
		g_pEnv->_resourceSystem->RegisterResourceLoader(this);
	}

	MeshLoader::~MeshLoader()
	{
		g_pEnv->_resourceSystem->UnregisterResourceLoader(this);
	}

	std::shared_ptr<IResource> MeshLoader::LoadResourceFromFile(const fs::path& absolutePath, FileSystem* fileSystem, const ResourceLoadOptions* options)
	{
		DiskFile file(absolutePath, std::ios::in | std::ios::binary);

		if (file.Open() == false)
		{
			LOG_CRIT("Unable to open file '%s' for reading", absolutePath.string().c_str());
			return nullptr;
		}

		std::shared_ptr<Mesh> mesh;

		bool hasAnimations = file.Read<bool>();

		if (hasAnimations)
		{
			mesh = std::shared_ptr<AnimatedMesh>(new AnimatedMesh(nullptr, fs::relative(absolutePath, fileSystem->GetDataDirectory()).string()), ResourceDeleter());

			AnimatedMesh* animatedMesh = reinterpret_cast<AnimatedMesh*>(mesh.get());

			// read the number of faces
			uint32_t numFaces;
			file.Read<uint32_t>(&numFaces);

			// Read the vertices
			uint32_t numVertices = file.Read<uint32_t>();
			std::vector<AnimatedMeshVertex> vertices(numVertices);
			file.Read((uint8_t*)vertices.data(), numVertices * sizeof(AnimatedMeshVertex));

			// read the index data
			uint32_t numIndices = file.Read<uint32_t>();
			std::vector<MeshIndexFormat> indices(numIndices);
			file.Read((uint8_t*)indices.data(), (uint32_t)indices.size() * sizeof(MeshIndexFormat));

			animatedMesh->SetNumFaces(numFaces);
			animatedMesh->AddVertices(vertices);
			animatedMesh->AddIndices(indices);

			auto animData = animatedMesh->CreateAnimationData();

			animData->_animations.resize(file.Read<uint32_t>());

			//for(uint32_t i = 0; i < animData->_animations.size(); ++i)
			for (auto& anim : animData->_animations)
			{
				//auto& anim = animData->_animations[i];

				anim.name = file.ReadString();

				anim.ticksPerSecond = file.Read<float>();
				anim.duration = file.Read<float>();

				auto rootNodeName = file.ReadString();

				anim.channels.resize(file.Read<uint32_t>());

				for (auto& chan : anim.channels)
				{
					chan.nodeName = file.ReadString();

					if (chan.nodeName == rootNodeName)
					{
						anim._rootNode = &chan;
					}

					chan.positionKeys.resize(file.Read<uint32_t>());
					for (auto& pk : chan.positionKeys)
					{
						pk.first = file.Read<float>();
						pk.second = file.Read<math::Vector3>();
					}

					chan.rotationKeys.resize(file.Read<uint32_t>());
					for (auto& pk : chan.rotationKeys)
					{
						pk.first = file.Read<float>();
						pk.second = file.Read<math::Quaternion>();
					}

					chan.scaleKeys.resize(file.Read<uint32_t>());
					for (auto& pk : chan.scaleKeys)
					{
						pk.first = file.Read<float>();
						pk.second = file.Read<math::Vector3>();
					}

					chan.nodeTransform = file.Read<math::Matrix>();
					chan.children.resize(file.Read<uint32_t>());
					
				}

				// read the child hierarchy
				for (auto& chan : anim.channels)
				{
					for (auto& child : chan.children)
					{
						auto childNodeName = file.ReadString();

						auto it = std::find_if(anim.channels.begin(), anim.channels.end(),
							[childNodeName](const AnimChannel& c)
							{
								return c.nodeName == childNodeName;
							});

						if (it != anim.channels.end())
						{
							child = &(*it);
						}
					}
				}
			}

			animData->_globalInverseTransform = file.Read<math::Matrix>();

			uint32_t numBones = file.Read<uint32_t>();

			AnimatedMesh::BoneNameMap bones;
			for (uint32_t i = 0; i < numBones; ++i)
			{
				auto boneName = file.ReadString();
				auto boneId = file.Read<uint32_t>();

				bones[boneName] = boneId;
			}

			AnimatedMesh::BoneInfoArray boneInfo;
			for (uint32_t i = 0; i < numBones; ++i)
			{
				BoneInfo bi = file.Read<BoneInfo>();
				boneInfo[i] = bi;
			}
			animatedMesh->SetBoneMap(numBones, bones, boneInfo);
			animatedMesh->SetAnimationData(animData);
		}
		else
		{
			mesh = std::shared_ptr<Mesh>(new Mesh(nullptr, fs::relative(absolutePath, fileSystem->GetDataDirectory()).string()), ResourceDeleter());

			// read the number of faces
			uint32_t numFaces;
			file.Read<uint32_t>(&numFaces);

			// Read the vertices
			uint32_t numVertices = file.Read<uint32_t>();
			std::vector<MeshVertex> vertices(numVertices);
			file.Read((uint8_t*)vertices.data(), numVertices * sizeof(MeshVertex));

			// read the index data
			uint32_t numIndices = file.Read<uint32_t>();
			std::vector<MeshIndexFormat> indices(numIndices);
			file.Read((uint8_t*)indices.data(), (uint32_t)indices.size() * sizeof(MeshIndexFormat));

			mesh->SetNumFaces(numFaces);
			mesh->AddVertices(vertices);
			mesh->AddIndices(indices);
		}

		// read the material
		bool hasMaterial = file.Read<bool>();
		std::string matName;

		if (hasMaterial)
		{
			matName = file.ReadString();
		}

		// force the animated shader, otherwise the mesh won't be animated
		if (hasAnimations)
		{
			matName = "EngineData.Materials/DefaultAnimated.hmat";
		}
		
		dx::BoundingBox aabb = file.Read<dx::BoundingBox>();
		dx::BoundingOrientedBox obb = file.Read<dx::BoundingOrientedBox>();	
		
		mesh->SetAABB(aabb);
		mesh->SetOBB(obb);

		if (matName.length() > 0)
		{
			mesh->SetMaterial(Material::Create(matName));
		}
		else
		{
			mesh->SetMaterial(Material::GetDefaultMaterial());
		}

		mesh->CreateBuffers();

		// finally close the file
		file.Close();

		return mesh;
	}

	std::shared_ptr<IResource> MeshLoader::LoadResourceFromMemory(const std::vector<uint8_t>& data, const fs::path& relativePath, FileSystem* fileSystem, const ResourceLoadOptions* options)
	{
		return nullptr;
	}

	void MeshLoader::UnloadResource(IResource* resource)
	{
		SAFE_DELETE(resource);
	}

	std::vector<std::string> MeshLoader::GetSupportedResourceExtensions()
	{
		return { ".hmesh" };
	}

	std::wstring MeshLoader::GetResourceDirectory() const
	{
		return L"Meshes";
	}

	Dialog* MeshLoader::CreateEditorDialog(const std::vector<fs::path>& paths)
	{
		return nullptr;
	}

	void MeshLoader::SaveResource(IResource* resource, const fs::path& path)
	{
		DiskFile file(path, std::ios::out | std::ios::binary | std::ios::trunc);

		if (file.Open() == false)
		{
			LOG_CRIT("Unable to open file '%S' for saving", path.wstring().c_str());
			return;
		}

		Mesh* mesh = dynamic_cast<Mesh*>(resource);

		if (mesh->HasAnimations())
		{
			file.Write<bool>(true);

			AnimatedMesh* animatedMesh = reinterpret_cast<AnimatedMesh*>(mesh);

			const auto& vertices = animatedMesh->GetVertices();
			const auto& indices = animatedMesh->GetIndices();

			// write the number of faces
			file.Write<uint32_t>(mesh->GetNumFaces());

			// write the vertex data
			file.Write<uint32_t>((uint32_t)vertices.size());
			file.Write((uint8_t*)vertices.data(), (uint32_t)vertices.size() * sizeof(AnimatedMeshVertex));

			// write the index data
			file.Write<uint32_t>((uint32_t)indices.size());
			file.Write((uint8_t*)indices.data(), (uint32_t)indices.size() * sizeof(MeshIndexFormat));

			auto animData = mesh->GetAnimationData();

			file.Write<uint32_t>((uint32_t)animData->_animations.size());

			for (auto& anim : animData->_animations)
			{
				file.WriteString(anim.name);
				file.Write<float>(anim.ticksPerSecond);
				file.Write<float>(anim.duration);

				file.WriteString(anim._rootNode ? anim._rootNode->nodeName : "");

				file.Write<uint32_t>(anim.channels.size());

				for (auto& chan : anim.channels)
				{
					file.WriteString(chan.nodeName);

					file.Write<uint32_t>(chan.positionKeys.size());
					for (auto& pk : chan.positionKeys)
					{
						file.Write<float>(pk.first);
						file.Write<math::Vector3>(pk.second);
					}

					file.Write<uint32_t>(chan.rotationKeys.size());
					for (auto& pk : chan.rotationKeys)
					{
						file.Write<float>(pk.first);
						file.Write<math::Quaternion>(pk.second);
					}

					file.Write<uint32_t>(chan.scaleKeys.size());
					for (auto& pk : chan.scaleKeys)
					{
						file.Write<float>(pk.first);
						file.Write<math::Vector3>(pk.second);
					}

					file.Write<math::Matrix>(chan.nodeTransform);

					file.Write<uint32_t>(chan.children.size());
				}

				for (auto& chan : anim.channels)
				{
					for (auto& child : chan.children)
					{
						file.WriteString(child->nodeName);
					}
				}
			}

			file.Write<math::Matrix>(animData->_globalInverseTransform);

			file.Write<uint32_t>(animatedMesh->GetBoneMap().size());

			for (auto& bone : animatedMesh->GetBoneMap())
			{
				file.WriteString(bone.first);
				file.Write<uint32_t>(bone.second);
			}

			auto& info = animatedMesh->GetAllBoneInfo();

			for(uint32_t i = 0; i < animatedMesh->GetNumBones(); ++i)
			{
				const auto& bi = info[i];

				file.Write<BoneInfo>(bi);
			}
		}
		else
		{
			file.Write<bool>(false);

			const auto& vertices = mesh->GetVertices();
			const auto& indices = mesh->GetIndices();

			// write the number of faces
			file.Write<uint32_t>(mesh->GetNumFaces());

			// write the vertex data
			file.Write<uint32_t>((uint32_t)vertices.size());
			file.Write((uint8_t*)vertices.data(), (uint32_t)vertices.size() * sizeof(MeshVertex));

			// write the index data
			file.Write<uint32_t>((uint32_t)indices.size());
			file.Write((uint8_t*)indices.data(), (uint32_t)indices.size() * sizeof(MeshIndexFormat));

			
		}		

		// write the material
		auto material = mesh->GetMaterial();
		if (material)
		{
			file.Write<bool>(true);
			file.WriteString(mesh->GetMaterial()->GetFileSystemPath().string());
		}
		else
		{
			file.Write<bool>(false);
		}

		

		// write the aabb + obb
		file.Write<dx::BoundingBox>(mesh->GetAABB());
		file.Write<dx::BoundingOrientedBox>(mesh->GetOBB());

		if (mesh->HasAnimations())
		{
			file.Write<bool>(true);

			
			//file.Write<uint32_t>(animData->_animIndex);
		}
		else
		{
			file.Write<bool>(false);
		}

		// finally close the file
		file.Close();
	}
}