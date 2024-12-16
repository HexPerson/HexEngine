
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

	class FileSystem;

	class IResource
	{
		friend class ResourceSystem;

	public:
		virtual ~IResource() {}

		void				SetLoader(class IResourceLoader* loader);
		IResourceLoader*	GetLoader() const;

		/// <summary>
		/// Allows the resource to be saved to disk
		/// </summary>
		virtual void	Save();

		/// <summary>
		/// Called when the resource is to be destroyed. This must be implemented
		/// </summary>
		virtual void	Destroy() = 0;

		/// <summary>
		/// Increase the reference count
		/// </summary>
		virtual void	AddRef();

		/// <summary>
		/// Decreases the reference count of this resource
		/// </summary>
		void			Release();

		/// <summary>
		/// Returns the current reference count
		/// </summary>
		/// <returns></returns>
		int32_t			GetRefCount() const;

		const fs::path& GetAbsolutePath() const;
		const fs::path& GetRelativePath() const;
		const fs::path& GetFileSystemPath() const;

		void			SetPaths(const fs::path& absolutePath, FileSystem* fileSystem);

		FileSystem*		GetOwningFileSystem() const;

	private:
		int32_t _refCnt = 0;		
		FileSystem* _fs = nullptr;

	protected:
		// this is the absolute path to the resource
		fs::path _absolutePath;

		// this is the path in filesystem format e.g. "EngineData.Textures/white.png"
		fs::path _fsRelativePath;

		// this is the path relative to the filesystem e.g. "Textures/white.png"
		fs::path _relativePath;

		class IResourceLoader* _loader = nullptr;
	};
}
