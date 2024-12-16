//
//
//#pragma once
//
//#include "../FileSystem/ResourceSystem.hpp"
//
//
//
//namespace HexEngine
//{
//	class FontImporter : public IResourceLoader
//	{
//	public:
//		FontImporter();
//		~FontImporter();
//
//		virtual IResource* LoadResource(const fs::path& absolutePath, const ResourceLoadOptions* options = nullptr) override;
//
//		virtual void UnloadResource(IResource* resource) override;
//
//		virtual std::vector<std::wstring> GetSupportedResourceExtensions() override;
//
//	private:
//		FT_Library _library;
//	};
//}
