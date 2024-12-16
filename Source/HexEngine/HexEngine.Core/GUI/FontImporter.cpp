//
//
//#include "FontImporter.hpp"
//#include "../Environment/IEnvironment.hpp"
//
//namespace HexEngine
//{
//	FontImporter::FontImporter()
//	{
//		g_pEnv->_resourceSystem->RegisterResourceLoader(this);
//
//		/*auto error = FT_Init_FreeType(&_library);
//		if (error)
//		{
//			LOG_CRIT("Failed to initialize font engine. Code: %d", error);
//		}*/
//	}
//
//	FontImporter::~FontImporter()
//	{
//		
//	}
//
//	IResource* FontImporter::LoadResource(const fs::path& absolutePath, const ResourceLoadOptions* options /*= nullptr*/)
//	{
//		//dx::SpriteFont* font = new dx::SpriteFont(g_pEnv->_graphicsModule->GetGraphicsDevice()->)
//		//FT_Face face;
//
//		//auto error = FT_New_Face(_library,
//		//	absolutePath.string().c_str(),
//		//	0,
//		//	&face);
//
//		//if (error)
//		//{
//		//	LOG_CRIT("Failed to import font face: %S. Code: %d", absolutePath.c_str(), error);
//		//	return nullptr;
//		//}
//
//		//error = FT_Set_Char_Size(
//		//	face,    /* handle to face object           */
//		//	0,       /* char_width in 1/64th of points  */
//		//	16 * 64,   /* char_height in 1/64th of points */
//		//	72,     /* horizontal device resolution    */
//		//	72);   /* vertical device resolution      */
//		return nullptr;
//	}
//
//	void FontImporter::UnloadResource(IResource* resource)
//	{
//		
//	}
//
//	std::vector<std::wstring> FontImporter::GetSupportedResourceExtensions()
//	{
//		return { L".ttf" };
//	}
//}