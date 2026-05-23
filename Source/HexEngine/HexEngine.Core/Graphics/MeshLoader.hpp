
#pragma once

#include "../Required.hpp"
#include "../FileSystem/ResourceSystem.hpp"

namespace HexEngine
{
	struct MeshLoadOptions : ResourceLoadOptions
	{
		bool createBuffers = true;
		bool createMaterial = true;
		bool populateVertices = true;
	};

	class MeshLoader : public IResourceLoader
	{
	public:
		MeshLoader();
		~MeshLoader();

		virtual std::shared_ptr<IResource>	LoadResourceFromFile(const fs::path& absolutePath, FileSystem* fileSystem, const ResourceLoadOptions* options = nullptr) override;
		virtual std::shared_ptr<IResource>	LoadResourceFromMemory(const std::vector<uint8_t>& data, const fs::path& relativePath, FileSystem* fileSystem, const ResourceLoadOptions* options = nullptr) override;
		virtual void						UnloadResource(IResource* resource) override;
		virtual void						OnResourceChanged(std::shared_ptr<IResource> resource) override {}
		virtual std::vector<std::string>	GetSupportedResourceExtensions() override;
		virtual std::wstring				GetResourceDirectory() const override;
		virtual Dialog*						CreateEditorDialog(const std::vector<fs::path>& paths) override;
		virtual void						SaveResource(IResource* resource, const fs::path& path) override;
		virtual bool						DoesSupportHotLoading() override { return true; }

	private:
		// Shared parsing body used by both the file-path and memory loading
		// entry points. Takes any DiskFile-derived reader (real DiskFile when
		// loading from disk, MemoryFile when loading from a .pkg buffer) - all
		// the binary parsing uses the inherited Read<T> / ReadString templates
		// which route through the virtual Read(void*, uint32_t) override.
		std::shared_ptr<IResource>	ParseMeshFromReader(class DiskFile& reader, const fs::path& sourcePath, const fs::path& relativeKey, FileSystem* fileSystem, const struct MeshLoadOptions* meshOpts);
	};
}