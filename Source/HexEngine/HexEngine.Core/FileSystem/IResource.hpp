
#pragma once

#include "../Required.hpp"
#include "DiskFile.hpp"

namespace HexEngine
{
	struct ResourceLoadOptions
	{
		bool silenceErrors = false;
		bool isLoadedFromAssetPackage = false;
	};

	typedef uint32_t ResourceId;
	constexpr uint32_t InvalidResourceId = (-1);

	class FileSystem;

	enum class ResourceType
	{
		None = 0,
		Image,
		Mesh,
		Audio,
		Font,
		Prefab,
		Material,
	};

	class HEX_API IResource
	{
		friend class ResourceSystem;

	public:
		virtual ~IResource() {}

		void				SetLoader(class IResourceLoader* loader);
		IResourceLoader*	GetLoader() const;

		ResourceId			GetId() const;

		/// <summary>
		/// Allows the resource to be saved to disk
		/// </summary>
		virtual void	Save();

		/// <summary>
		/// Called when the resource is to be destroyed. This must be implemented
		/// </summary>
		virtual void	Destroy() = 0;

		const fs::path& GetAbsolutePath() const;
		const fs::path& GetRelativePath() const;
		const fs::path& GetFileSystemPath() const;

		void			SetPaths(const fs::path& absolutePath, FileSystem* fileSystem);

		FileSystem*		GetOwningFileSystem() const;

		virtual ResourceType GetResourceType() const;

	private:	
		FileSystem* _fs = nullptr;

	protected:
		// this is the absolute path to the resource
		fs::path _absolutePath;

		// this is the path in filesystem format e.g. "EngineData.Textures/white.png"
		fs::path _fsRelativePath;

		// this is the path relative to the filesystem e.g. "Textures/white.png"
		fs::path _relativePath;

		class IResourceLoader* _loader = nullptr;

		uint32_t _id = InvalidResourceId;
	};
}
