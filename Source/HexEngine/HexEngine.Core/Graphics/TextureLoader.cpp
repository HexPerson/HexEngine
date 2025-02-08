
#include "TextureLoader.hpp"

namespace HexEngine
{
	std::shared_ptr<IResource> TextureLoader::LoadResourceFromFile(const fs::path& absolutePath, FileSystem* fileSystem, const ResourceLoadOptions* options)
	{
		//std::shared_ptr<Texture2D> texture = 
		return nullptr;
	}

	std::shared_ptr<IResource> TextureLoader::LoadResourceFromMemory(const std::vector<uint8_t>& data, const fs::path& relativePath, FileSystem* fileSystem, const ResourceLoadOptions* options)
	{
		return nullptr;
	}

	void TextureLoader::UnloadResource(IResource* resource)
	{
		SAFE_DELETE(resource);
	}

	std::vector<std::string> TextureLoader::GetSupportedResourceExtensions()
	{
		return { ".htex" };
	}

	std::wstring TextureLoader::GetResourceDirectory() const
	{
		return L"Textures";
	}

	Dialog* TextureLoader::CreateEditorDialog(const std::vector<fs::path>& paths)
	{
		return nullptr;
	}

	void TextureLoader::SaveResource(IResource* resource, const fs::path& path)
	{

	}
}