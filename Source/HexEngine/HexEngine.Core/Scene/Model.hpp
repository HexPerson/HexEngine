
#pragma once

#include "../Required.hpp"
#include "Mesh.hpp"
#include "../FileSystem/IResource.hpp"
#include "../FileSystem/ResourceSystem.hpp"

namespace HexEngine
{
	class FileSystem;


	class HEX_API Model : public IResource
	{
		friend class AssimpModelImporter;

	public:
		static std::shared_ptr<Model> Create(const fs::path& path);
		static std::shared_ptr<Model> CreateAsync(const fs::path& path, ResourceLoadedFn callback);

		Model() {}
		virtual ~Model();

		void SetPaths(const fs::path& path, FileSystem* fileSystem);

		fs::path GetRelativePath();

		virtual void Destroy() override;

		std::shared_ptr<Mesh> GetMeshAtIndex(uint32_t index);

		const std::vector<std::shared_ptr<Mesh>>& GetMeshes() const;

		uint32_t GetNumMeshes();

		void AddMesh(const std::shared_ptr<Mesh>& mesh);

		int32_t GetMaxLOD() { return _maxLOD; }
		void SetMaxLOD(int32_t maxLod) { _maxLOD = maxLod; }

	private:
		std::vector<std::shared_ptr<Mesh>> _meshes;
		std::wstring _name;
		fs::path _relativePath;
		int32_t _maxLOD = -1;

		std::shared_ptr<AnimationData> _animData;
	};
}