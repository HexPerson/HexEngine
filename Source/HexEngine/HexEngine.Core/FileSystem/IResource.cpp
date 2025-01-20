
#include "IResource.hpp"
#include "ResourceSystem.hpp"
#include "FileSystem.hpp"

namespace HexEngine
{
	void IResource::SetLoader(class IResourceLoader* loader)
	{
		_loader = loader;
	}

	IResourceLoader* IResource::GetLoader() const
	{
		return _loader;
	}

	void IResource::Save()
	{
		if (_loader)
			_loader->SaveResource(this, GetAbsolutePath());
	}

	uint32_t IResource::GetId() const
	{
		return _id;
	}

	const fs::path& IResource::GetAbsolutePath() const
	{
		return _absolutePath;
	}

	const fs::path& IResource::GetRelativePath() const
	{
		return _relativePath;
	}

	const fs::path& IResource::GetFileSystemPath() const
	{
		return _fsRelativePath;
	}

	FileSystem* IResource::GetOwningFileSystem() const
	{
		return _fs;
	}

	void IResource::SetPaths(const fs::path& absolutePath, FileSystem* fileSystem)
	{
		_relativePath = fs::relative(absolutePath, fileSystem->GetDataDirectory());
		_fsRelativePath = fileSystem->GetRelativeResourcePath(_relativePath);
		_absolutePath = absolutePath;
	}
}