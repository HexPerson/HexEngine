
#pragma once

#include <HexEngine.Core\HexEngine.hpp>

#undef min
#undef max

#include <assimp\Importer.hpp>
#include <assimp\scene.h>
#include <assimp\postprocess.h>
#include <assimp\IOSystem.hpp>
#include <assimp\IOStream.hpp>

namespace HexEngine
{	
	class AssimpIoStream : public Assimp::IOStream
	{
	public:
		AssimpIoStream(const char* pFile, const char* pMode);

		virtual size_t Read(void* pvBuffer,	size_t pSize, size_t pCount) override;

		virtual size_t Write(const void* pvBuffer, size_t pSize, size_t pCount) override;

		virtual aiReturn Seek(size_t pOffset, aiOrigin pOrigin) override;

		virtual size_t Tell() const override;

		virtual size_t FileSize() const override;

		virtual void Flush() override;

	private:
		fs::path _path;
		std::vector<uint8_t> _fileData;
		size_t _readOffset;
	};

	class AssimpIoSystem : public Assimp::IOSystem
	{
	public:
		virtual bool Exists(const char* pFile) const override;
		virtual AssimpIoStream* Open(const char* pFile, const char* pMode = "rb") override;
		virtual void Close(Assimp::IOStream* pFile) override;
		virtual char getOsSeparator() const override { return '\\'; }
	};	

	struct AssimpImportOptions
	{
		bool importAnimations = true;
		bool tryAndCreateMaterials = false;
		bool renameFiles = true;
		bool deleteOriginalsAfterImport = true;
		std::wstring textureSearchPath;
		std::wstring replaceTextureExtension;
	};

	class AssimpModelImporter : public IModelImporter
	{
	public:
		AssimpModelImporter();
		~AssimpModelImporter();

		virtual bool Create() override { return true; }

		virtual void Destroy() override;

		virtual std::shared_ptr<IResource>	LoadResourceFromFile(const fs::path& path, FileSystem* fileSystem, const ResourceLoadOptions* options = nullptr) override;
		virtual std::shared_ptr<IResource>	LoadResourceFromMemory(const std::vector<uint8_t>& data, const fs::path& relativePath, FileSystem* fileSystem, const ResourceLoadOptions* options = nullptr) override;
		virtual void						OnResourceChanged(std::shared_ptr<IResource> resource) override {}
		virtual void						UnloadResource(IResource* resource) override;
		virtual std::vector<std::string>	GetSupportedResourceExtensions() override;
		virtual std::wstring				GetResourceDirectory() const override;
		virtual Dialog*						CreateEditorDialog(const std::vector<fs::path>& paths) override;
		virtual void						SaveResource(IResource* resource, const fs::path& path) override { }

	private:
		void							ProcessNode(std::shared_ptr<Model>& model, aiNode* node, std::vector<AnimChannel*> parentAnims, const aiScene* scene, FileSystem* fileSystem);
		std::shared_ptr<Mesh>			ProcessMesh(std::shared_ptr<Model>& model, aiMesh* mesh, const aiScene* scene, aiNode* node, FileSystem* fileSystem);
		std::shared_ptr<AnimatedMesh>	ProcessAnimatedMesh(std::shared_ptr<Model>& model, aiMesh* mesh, const aiScene* scene, aiNode* node, FileSystem* fileSystem);
		void							ProcessMaterial(std::shared_ptr<Mesh> mesh, const aiScene* scene, aiMaterial* material, FileSystem* fileSystem);
		std::shared_ptr<ITexture2D>		LoadTexture(std::shared_ptr<Mesh>& mesh, const aiTextureType type, const aiScene* scene, const aiMaterial* material, FileSystem* fileSystem);
		void							ProcessAnimations(std::shared_ptr<Model>& model, const aiScene* scene);
		AnimChannel*					FindAnimChannelFromNodeName(std::shared_ptr<Model>& model, const std::string& nodeName);

	private:
		fs::path _currentPath;
		Mesh* _loadedMesh = nullptr;
		Assimp::IOSystem* _defaultIoSystem = nullptr;

		// Import options
		AssimpImportOptions _importOpts;

		std::vector<std::pair<fs::path, std::shared_ptr<Mesh>>> _createdMeshes;
	};
}
