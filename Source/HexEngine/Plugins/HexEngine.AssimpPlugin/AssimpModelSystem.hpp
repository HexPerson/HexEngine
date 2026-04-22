
#pragma once

#include <HexEngine.Core\HexEngine.hpp>

#undef min
#undef max

#include <assimp\Importer.hpp>
#include <assimp\scene.h>
#include <assimp\postprocess.h>
#include <assimp\IOSystem.hpp>
#include <assimp\IOStream.hpp>

class AssimpIoStream : public Assimp::IOStream
{
public:
	AssimpIoStream(const char* pFile, const char* pMode);

	virtual size_t Read(void* pvBuffer, size_t pSize, size_t pCount) override;

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
	bool mergeChildMeshesByMaterial = false;
	bool renameFiles = true;
	bool deleteOriginalsAfterImport = true;
	float importScale = 1.0f;
	std::vector<std::wstring> textureSearchPaths;
	std::wstring replaceTextureExtension;
	std::wstring texturePrefix;
};

class AssimpModelImporter : public HexEngine::IModelImporter
{
public:
	AssimpModelImporter();
	~AssimpModelImporter();

	virtual bool Create() override { return true; }

	virtual void Destroy() override;

	virtual std::shared_ptr<HexEngine::IResource>	LoadResourceFromFile(const fs::path& path, HexEngine::FileSystem* fileSystem, const HexEngine::ResourceLoadOptions* options = nullptr) override;
	virtual std::shared_ptr<HexEngine::IResource>	LoadResourceFromMemory(const std::vector<uint8_t>& data, const fs::path& relativePath, HexEngine::FileSystem* fileSystem, const HexEngine::ResourceLoadOptions* options = nullptr) override;
	virtual void						OnResourceChanged(std::shared_ptr<HexEngine::IResource> resource) override {}
	virtual void						UnloadResource(HexEngine::IResource* resource) override;
	virtual std::vector<std::string>	GetSupportedResourceExtensions() override;
	virtual std::wstring				GetResourceDirectory() const override;
	virtual HexEngine::Dialog* CreateEditorDialog(const std::vector<fs::path>& paths) override;
	virtual void						SaveResource(HexEngine::IResource* resource, const fs::path& path) override {}
	virtual bool						DoesSupportHotLoading() override { return true; }

private:
	void							ProcessNode(std::shared_ptr<HexEngine::Model>& model, aiNode* node, std::vector<HexEngine::AnimChannel*> parentAnims, const aiScene* scene, HexEngine::FileSystem* fileSystem);
	void							ProcessMergedStaticMeshes(std::shared_ptr<HexEngine::Model>& model, const aiScene* scene, HexEngine::FileSystem* fileSystem);
	std::shared_ptr<HexEngine::Mesh>			ProcessMesh(std::shared_ptr<HexEngine::Model>& model, aiMesh* mesh, const aiScene* scene, aiNode* node, HexEngine::FileSystem* fileSystem);
	std::shared_ptr<HexEngine::AnimatedMesh>	ProcessAnimatedMesh(std::shared_ptr<HexEngine::Model>& model, aiMesh* mesh, const aiScene* scene, aiNode* node, HexEngine::FileSystem* fileSystem);
	void							ProcessMaterial(std::shared_ptr<HexEngine::Mesh> mesh, const aiScene* scene, aiMaterial* material, HexEngine::FileSystem* fileSystem);
	std::shared_ptr<HexEngine::ITexture2D>		LoadTexture(std::shared_ptr<HexEngine::Mesh>& mesh, const aiTextureType type, const aiScene* scene, const aiMaterial* material, HexEngine::FileSystem* fileSystem);
	void							ProcessAnimations(std::shared_ptr<HexEngine::Model>& model, const aiScene* scene);
	HexEngine::AnimChannel* FindAnimChannelFromNodeName(std::shared_ptr<HexEngine::Model>& model, const std::string& nodeName);
	bool OnBrowseFolderPath(HexEngine::LineEdit* edit);

private:
	fs::path _currentPath;
	HexEngine::Mesh* _loadedMesh = nullptr;
	Assimp::IOSystem* _defaultIoSystem = nullptr;

	// Import options
	AssimpImportOptions _importOpts;

	std::vector<std::pair<fs::path, std::shared_ptr<HexEngine::Mesh>>> _createdMeshes;
};
