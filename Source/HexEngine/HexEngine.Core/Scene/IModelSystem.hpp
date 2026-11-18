
#pragma once

#include "../Required.hpp"
#include "Model.hpp"
#include "../FileSystem/ResourceSystem.hpp"
#include "../Plugin/IPlugin.hpp"

namespace HexEngine
{
	/** @brief Options for IModelImporter::ImportModel (the editor's import dialog, without the dialog). */
	struct ModelImportSettings
	{
		bool importAnimations = true;
		bool createMaterials = true;            // write .hmat files from the model's materials
		bool mergeChildMeshesByMaterial = false;
		float importScale = 1.0f;
		std::vector<std::wstring> textureSearchPaths;
	};

	class IModelImporter : public IResourceLoader, public IPluginInterface
	{
	public:
		DECLARE_PLUGIN_INTERFACE(IModelImporter, 001);

		/**
		 * @brief Imports a model file (fbx, gltf/glb, obj, ...) into engine assets
		 *        without UI: writes .hmesh files (and .hmat with createMaterials)
		 *        next to `absolutePath`, which must be inside `fileSystem`'s Data
		 *        folder. The source file is left in place.
		 * @param createdMeshes receives the absolute paths of the written .hmesh files.
		 */
		virtual bool ImportModel(const fs::path& absolutePath, FileSystem* fileSystem, const ModelImportSettings& settings,
			std::vector<fs::path>& createdMeshes, std::string& error)
		{
			(void)absolutePath; (void)fileSystem; (void)settings; (void)createdMeshes;
			error = "this model importer has no dialog-free import";
			return false;
		}
	};
}
