
#include "MeshLoader.hpp"
#include "../Environment/IEnvironment.hpp"
#include "../Environment/LogFile.hpp"
#include "../Scene/Mesh.hpp"
#include "../Scene/AnimatedMesh.hpp"
#include "../FileSystem/FileSystem.hpp"
#include "../FileSystem/DiskFile.hpp"
#include "../FileSystem/BinaryReader.hpp"
#include "Material.hpp"

namespace HexEngine
{
	MeshLoader::MeshLoader()
	{
		g_pEnv->GetResourceSystem().RegisterResourceLoader(this);
	}

	MeshLoader::~MeshLoader()
	{
		g_pEnv->GetResourceSystem().UnregisterResourceLoader(this);
	}

	std::shared_ptr<IResource> MeshLoader::LoadResourceFromFile(const fs::path& absolutePath, FileSystem* fileSystem, const ResourceLoadOptions* options)
	{
		const MeshLoadOptions* meshOpts = reinterpret_cast<const MeshLoadOptions*>(options);

		DiskFile file(absolutePath, std::ios::in | std::ios::binary);

		if (file.Open() == false)
		{
			LOG_CRIT("Unable to open file '%s' for reading", absolutePath.string().c_str());
			return nullptr;
		}

		LOG_INFO("Loading mesh '%s'", absolutePath.filename().string().c_str());

		// Slurp the whole .hmesh into memory and parse it through a
		// bounds-checked BinaryReader. The parser never trusts a file-provided
		// count or length - every allocation is validated against the bytes
		// actually present - so a corrupt or hostile .hmesh fails cleanly
		// instead of over-allocating or reading out of bounds.
		std::vector<uint8_t> bytes;
		file.ReadAll(bytes);
		file.Close();

		// Use absolutePath both as "where I came from" (for resolving the
		// relative mesh name used when constructing Mesh/AnimatedMesh) and as
		// the cache key for the loaded resource - the file path is the canonical
		// identity for disk-loaded meshes.
		BinaryReader reader(bytes);
		return ParseMeshFromReader(reader, absolutePath, absolutePath, fileSystem, meshOpts);
	}

	std::shared_ptr<IResource> MeshLoader::LoadResourceFromMemory(const std::vector<uint8_t>& data, const fs::path& relativePath, FileSystem* fileSystem, const ResourceLoadOptions* options)
	{
		const MeshLoadOptions* meshOpts = reinterpret_cast<const MeshLoadOptions*>(options);

		if (data.empty())
		{
			LOG_CRIT("Mesh memory buffer for '%s' is empty or invalid", relativePath.string().c_str());
			return nullptr;
		}

		LOG_INFO("Loading packaged mesh '%s'", relativePath.filename().string().c_str());

		// The .pkg already handed us the full asset bytes; parse them in place.
		BinaryReader reader(data);
		return ParseMeshFromReader(reader, relativePath, relativePath, fileSystem, meshOpts);
	}

	// Bail out of a parse on the first sign of corruption/truncation. `sourcePath`
	// is captured from the enclosing function's parameter.
	#define MESH_REQUIRE(expr) do { if (!(expr)) { \
		LOG_CRIT("Corrupt or truncated mesh '%s' (failed check: %s)", sourcePath.string().c_str(), #expr); \
		return nullptr; } } while(0)

	std::shared_ptr<IResource> MeshLoader::ParseMeshFromReader(BinaryReader& r, const fs::path& sourcePath, const fs::path& relativeKey, FileSystem* fileSystem, const MeshLoadOptions* meshOpts)
	{
		// Conservative lower bounds on the on-disk byte footprint of one element,
		// used to bound a file-provided count BEFORE we resize(): count * floor
		// must fit the bytes that remain, so a bogus count can't drive a huge
		// allocation. Exact for fixed-size records; a hard lower bound for the
		// variable-size ones (which assume empty strings / zero sub-counts - the
		// smallest a real element can be), which is all the guard needs.
		const size_t kAnimFloor    = 4 + 4 + 4 + 4 + 4 + sizeof(math::Matrix); // name+ticks+dur+root+chanCount+globalInv
		const size_t kChannelFloor = 4 + 4 + 4 + 4 + sizeof(math::Matrix) + 4; // name+3 keyCounts+nodeXform+childCount
		const size_t kNameFloor    = 4;                                        // a length-prefixed string is >= 4 bytes
		const size_t kPosKeyBytes  = sizeof(float) + sizeof(math::Vector3);
		const size_t kRotKeyBytes  = sizeof(float) + sizeof(math::Quaternion);
		const size_t kScaleKeyBytes = sizeof(float) + sizeof(math::Vector3);

		std::shared_ptr<Mesh> mesh;

		bool hasAnimations = false;
		MESH_REQUIRE(r.Read(hasAnimations));

		if (hasAnimations)
		{
			mesh = std::shared_ptr<AnimatedMesh>(new AnimatedMesh(nullptr, fs::relative(sourcePath, fileSystem->GetDataDirectory()).string()), ResourceDeleter());

			AnimatedMesh* animatedMesh = reinterpret_cast<AnimatedMesh*>(mesh.get());

			// read the number of faces
			uint32_t numFaces = 0;
			MESH_REQUIRE(r.Read(numFaces));

			// Read the vertices
			uint32_t numVertices = 0;
			MESH_REQUIRE(r.ReadCount(numVertices, sizeof(AnimatedMeshVertex)));
			std::vector<AnimatedMeshVertex> vertices(numVertices);
			if (numVertices > 0)
				MESH_REQUIRE(r.ReadBytes(vertices.data(), (size_t)numVertices * sizeof(AnimatedMeshVertex)));

			// read the index data
			uint32_t numIndices = 0;
			MESH_REQUIRE(r.ReadCount(numIndices, sizeof(MeshIndexFormat)));
			std::vector<MeshIndexFormat> indices(numIndices);
			if (numIndices > 0)
				MESH_REQUIRE(r.ReadBytes(indices.data(), (size_t)numIndices * sizeof(MeshIndexFormat)));

			animatedMesh->SetNumFaces(numFaces);
			animatedMesh->AddVertices(vertices);
			animatedMesh->AddIndices(indices);

			auto animData = animatedMesh->CreateAnimationData();

			uint32_t numAnims = 0;
			MESH_REQUIRE(r.ReadCount(numAnims, kAnimFloor));
			animData->_animations.resize(numAnims);

			for (auto& anim : animData->_animations)
			{
				MESH_REQUIRE(r.ReadString(anim.name));

				MESH_REQUIRE(r.Read(anim.ticksPerSecond));
				MESH_REQUIRE(r.Read(anim.duration));

				std::string rootNodeName;
				MESH_REQUIRE(r.ReadString(rootNodeName));

				uint32_t numChannels = 0;
				MESH_REQUIRE(r.ReadCount(numChannels, kChannelFloor));
				anim.channels.resize(numChannels);

				for (auto& chan : anim.channels)
				{
					MESH_REQUIRE(r.ReadString(chan.nodeName));

					if (chan.nodeName == rootNodeName)
					{
						anim._rootNode = &chan;
					}

					uint32_t numPosKeys = 0;
					MESH_REQUIRE(r.ReadCount(numPosKeys, kPosKeyBytes));
					chan.positionKeys.resize(numPosKeys);
					for (auto& pk : chan.positionKeys)
					{
						MESH_REQUIRE(r.Read(pk.first));
						MESH_REQUIRE(r.Read(pk.second));
					}

					uint32_t numRotKeys = 0;
					MESH_REQUIRE(r.ReadCount(numRotKeys, kRotKeyBytes));
					chan.rotationKeys.resize(numRotKeys);
					for (auto& pk : chan.rotationKeys)
					{
						MESH_REQUIRE(r.Read(pk.first));
						MESH_REQUIRE(r.Read(pk.second));
					}

					uint32_t numScaleKeys = 0;
					MESH_REQUIRE(r.ReadCount(numScaleKeys, kScaleKeyBytes));
					chan.scaleKeys.resize(numScaleKeys);
					for (auto& pk : chan.scaleKeys)
					{
						MESH_REQUIRE(r.Read(pk.first));
						MESH_REQUIRE(r.Read(pk.second));
					}

					MESH_REQUIRE(r.Read(chan.nodeTransform));

					uint32_t numChildren = 0;
					MESH_REQUIRE(r.ReadCount(numChildren, kNameFloor));
					chan.children.resize(numChildren);
				}

				// read the child hierarchy
				for (auto& chan : anim.channels)
				{
					for (auto& child : chan.children)
					{
						std::string childNodeName;
						MESH_REQUIRE(r.ReadString(childNodeName));

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

				// Per-animation root-coordinate-frame transform. Mixamo-style
				// sibling-merged FBX content needs this to be the SOURCE
				// scene's root transform, not the primary's, otherwise the
				// runtime applies the wrong axis flip / unit scale and the
				// mesh renders 90-degrees rotated or sheared. Writer always
				// emits the matrix. Reader expects it; pre-this-format-bump
				// .hmesh files must be re-imported (only animated meshes
				// affected - static .hmesh files use the unanimated branch
				// above which is unchanged).
				MESH_REQUIRE(r.Read(anim._globalInverseTransform));
			}

			MESH_REQUIRE(r.Read(animData->_globalInverseTransform));

			uint32_t numBones = 0;
			MESH_REQUIRE(r.Read(numBones));
			// The bone info is stored in a fixed std::array<BoneInfo, MAX_BONES>;
			// a file-provided count above the cap would index out of bounds in the
			// loop below (before SetBoneMap's own guard could reject it), so refuse
			// the file outright.
			MESH_REQUIRE(numBones <= (uint32_t)MAX_BONES);

			AnimatedMesh::BoneNameMap bones;
			for (uint32_t i = 0; i < numBones; ++i)
			{
				std::string boneName;
				MESH_REQUIRE(r.ReadString(boneName));
				uint32_t boneId = 0;
				MESH_REQUIRE(r.Read(boneId));

				bones[boneName] = boneId;
			}

			AnimatedMesh::BoneInfoArray boneInfo;
			for (uint32_t i = 0; i < numBones; ++i)
			{
				BoneInfo bi{};
				MESH_REQUIRE(r.Read(bi));
				boneInfo[i] = bi;
			}
			animatedMesh->SetBoneMap(numBones, bones, boneInfo);
			animatedMesh->SetAnimationData(animData);
		}
		else
		{
			mesh = std::shared_ptr<Mesh>(new Mesh(nullptr, fs::relative(sourcePath, fileSystem->GetDataDirectory()).string()), ResourceDeleter());

			// read the number of faces
			uint32_t numFaces = 0;
			MESH_REQUIRE(r.Read(numFaces));

			// Read the vertices
			uint32_t numVertices = 0;
			MESH_REQUIRE(r.ReadCount(numVertices, sizeof(MeshVertex)));
			std::vector<MeshVertex> vertices(numVertices);
			if (numVertices > 0)
				MESH_REQUIRE(r.ReadBytes(vertices.data(), (size_t)numVertices * sizeof(MeshVertex)));

			// read the index data
			uint32_t numIndices = 0;
			MESH_REQUIRE(r.ReadCount(numIndices, sizeof(MeshIndexFormat)));
			std::vector<MeshIndexFormat> indices(numIndices);
			if (numIndices > 0)
				MESH_REQUIRE(r.ReadBytes(indices.data(), (size_t)numIndices * sizeof(MeshIndexFormat)));

			if ((meshOpts && meshOpts->populateVertices) || meshOpts == nullptr)
			{
				mesh->SetNumFaces(numFaces);
				mesh->AddVertices(vertices);
				mesh->AddIndices(indices);
			}
		}

		// read the material
		bool hasMaterial = false;
		MESH_REQUIRE(r.Read(hasMaterial));
		std::string matName;

		if (hasMaterial)
		{
			MESH_REQUIRE(r.ReadString(matName));
		}

		// force the animated shader, otherwise the mesh won't be animated
		if (hasAnimations)
		{
			matName = "EngineData.Materials/DefaultAnimated.hmat";
		}

		mesh->SetMaterialName(matName);

		dx::BoundingBox aabb{};
		dx::BoundingOrientedBox obb{};
		MESH_REQUIRE(r.Read(aabb));
		MESH_REQUIRE(r.Read(obb));

		mesh->SetAABB(aabb);
		mesh->SetOBB(obb);

		// Final integrity gate: if any read past this point (or earlier scalar
		// reads) ran the stream dry, the reader has latched failure - reject the
		// whole mesh rather than hand back a half-populated one.
		MESH_REQUIRE(r.Good());

		if ((meshOpts && meshOpts->createMaterial) || meshOpts == nullptr)
		{
			if (matName.length() > 0)
			{
				mesh->SetMaterial(Material::Create(matName));
			}
			else
			{
				mesh->SetMaterial(Material::GetDefaultMaterial());
			}
		}

		if((meshOpts && meshOpts->createBuffers) || meshOpts == nullptr)
			mesh->CreateBuffers();

		(void)relativeKey;
		return mesh;
	}

	#undef MESH_REQUIRE

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
		DiskFile file(path, std::ios::out | std::ios::binary | std::ios::trunc, DiskFileOptions::CreateSubDirs);

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

				file.Write<uint32_t>((uint32_t)anim.channels.size());

				for (auto& chan : anim.channels)
				{
					file.WriteString(chan.nodeName);

					file.Write<uint32_t>((uint32_t)chan.positionKeys.size());
					for (auto& pk : chan.positionKeys)
					{
						file.Write<float>(pk.first);
						file.Write<math::Vector3>(pk.second);
					}

					file.Write<uint32_t>((uint32_t)chan.rotationKeys.size());
					for (auto& pk : chan.rotationKeys)
					{
						file.Write<float>(pk.first);
						file.Write<math::Quaternion>(pk.second);
					}

					file.Write<uint32_t>((uint32_t)chan.scaleKeys.size());
					for (auto& pk : chan.scaleKeys)
					{
						file.Write<float>(pk.first);
						file.Write<math::Vector3>(pk.second);
					}

					file.Write<math::Matrix>(chan.nodeTransform);

					file.Write<uint32_t>((uint32_t)chan.children.size());
				}

				for (auto& chan : anim.channels)
				{
					for (auto& child : chan.children)
					{
						file.WriteString(child->nodeName);
					}
				}

				// Per-animation root-coordinate-frame transform. Captured
				// from the SOURCE FBX scene (primary OR sibling) when the
				// animation was imported; sibling-merged clips need this to
				// be different from the primary's so the runtime applies
				// the right axis flip / unit scale at playback. See the
				// matching Read site above.
				file.Write<math::Matrix>(anim._globalInverseTransform);
			}

			file.Write<math::Matrix>(animData->_globalInverseTransform);

			file.Write<uint32_t>((uint32_t)animatedMesh->GetBoneMap().size());

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

		// finally close the file
		file.Close();
	}
}
