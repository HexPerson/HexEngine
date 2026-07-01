
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
		// entry points. Both entries materialise the full asset bytes (slurped
		// from disk, or handed over by the .pkg) and parse them through a
		// bounds-checked BinaryReader: every file-provided count/length is
		// validated against the bytes actually present before it drives an
		// allocation or a bulk read, so a corrupt/hostile .hmesh fails cleanly.
		std::shared_ptr<IResource>	ParseMeshFromReader(class BinaryReader& r, const fs::path& sourcePath, const fs::path& relativeKey, FileSystem* fileSystem, const struct MeshLoadOptions* meshOpts);
	};
}