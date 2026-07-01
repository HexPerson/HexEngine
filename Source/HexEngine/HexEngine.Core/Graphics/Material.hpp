

#pragma once

#include "ITexture2D.hpp"
#include "ITexture3D.hpp"
#include "IShader.hpp"
#include "RenderStructs.hpp"
#include "MaterialGraph.hpp"
#include "../FileSystem/IResource.hpp"
#include "../FileSystem/JsonFile.hpp"
#include <atomic>

namespace HexEngine
{
	

	class HEX_API Material : public IResource
	{
	public:
		Material();
		~Material();

		bool Equals(const Material& other)
		{
			std::unique_lock lock(_lock);

			bool texturesAreMatching = false;

			for (int32_t i = 0; i < MaterialTexture::Count; ++i)
			{
				if (_textures[i] != 0 && other._textures[i] != 0)
				{
					if (_textures[i]->GetAbsolutePath() == other._textures[i]->GetAbsolutePath())
					{
						texturesAreMatching = true;
					}
					else
					{
						texturesAreMatching = false;
					}
				}
			}
			return (
				texturesAreMatching &&
				GetName() == other.GetName() &&
				_properties == other._properties &&
				_standardShader == other._standardShader &&
				_shadowMapShader == other._shadowMapShader &&
				_blendState == other._blendState &&
				_depthState == other._depthState &&
				_cullMode == other._cullMode &&
				_affectsGI == other._affectsGI &&
				_emissiveAffectsGI == other._emissiveAffectsGI
				);
		}

		bool operator ==(const Material& other)
		{
			return Equals(other);
		}

		bool operator ==(const Material* other)
		{
			return Equals(*other);
		}

		void SetName(const std::string& name);
		const std::string& GetName() const;

		// Resource path of the footstep sound played when a character walks on a
		// surface using this material (empty = use the controller's default).
		void SetFootstepSoundPath(const std::string& path);
		const std::string& GetFootstepSoundPath() const;

		// Per-region footstep surfaces for atlas / splatmap materials. The surface
		// map is a texture aligned to the albedo UVs whose RED channel encodes a
		// surface id (0..N-1); the footstep system samples it at the hit UV and
		// plays the matching entry from the surface-sound table. Empty map = fall
		// back to the single GetFootstepSoundPath(). See FirstPersonCameraController.
		void SetFootstepSurfaceMapPath(const std::string& path);
		const std::string& GetFootstepSurfaceMapPath() const;
		void SetFootstepSurfaceSounds(const std::vector<std::string>& sounds);
		const std::vector<std::string>& GetFootstepSurfaceSounds() const;
		// Assigns the sound for a single surface id, growing the table as needed.
		void SetFootstepSurfaceSound(int32_t id, const std::string& path);
		// Sound for surface id, or empty string if id is unmapped / out of range.
		const std::string& GetFootstepSurfaceSound(int32_t id) const;

		static std::shared_ptr<Material> Create(const fs::path& path);
		static std::shared_ptr<Material> CreateAsync(const fs::path& path, ResourceLoadedFn fn);
		static std::shared_ptr<Material> GetDefaultMaterial();
		static bool Exists(const fs::path& path);
		static const std::wstring& GetMaterialTextureName(MaterialTexture type);

		virtual void Destroy() override;

		Material(const Material& other);

		void CopyFrom(const std::shared_ptr<Material>& material);
		void CopyFrom(const Material& material);

		Material& operator = (const Material& other);

		Material& operator = (const std::shared_ptr<Material>& other);

		void SetTexture(MaterialTexture type, const std::shared_ptr<ITexture2D>& texture);

		void SetStandardShader(const std::shared_ptr<IShader>& shader);
		void SetShadowMapShader(const std::shared_ptr<IShader>& shader);

		std::shared_ptr<IShader> GetStandardShader() const;
		std::shared_ptr<IShader> GetShadowMapShader() const;

		const std::shared_ptr<ITexture2D>& GetTexture(MaterialTexture type) const;
		uint32_t GetObjectFlags() const;

		void SetEmissiveAffectsGI(bool value);
		bool GetEmissiveAffectsGI() const;
		void SetAffectsGI(bool value);
		bool GetAffectsGI() const;

		void				SetBlendState(BlendState state);
		void				SetCullMode(CullingMode mode);
		void				SetDepthState(DepthBufferState state);
		void				SetFormat(MaterialFormat format);
		void				SetCullDistance(float distance);

		BlendState			GetBlendState() const;
		CullingMode			GetCullMode() const;
		DepthBufferState	GetDepthState() const;
		MaterialFormat		GetFormat() const;
		float				GetCullDistance() const;

		void		SaveRenderState();
		void		RestoreRenderState();

		void AddSoundTag(const std::string& key, const std::string& value);
		const std::string& GetSoundTag(const std::string& key) const;

		void Lock();
		void Unlock();
		void IncrementEditorOpenCount();
		void DecrementEditorOpenCount();
		bool IsHotReloadSuppressed() const;

		bool DoesHaveAnyReflectivity();

	public:
		MaterialProperties _properties;
		MaterialGraph _graph;
		MaterialGraphInstanceData _graphInstance;
		bool _hasGraph = false;
		bool _hasGraphInstance = false;

	private:
		uint32_t _materialId = 0;
		std::shared_ptr<ITexture2D> _textures[MaterialTexture::Count];
		std::shared_ptr<IShader> _standardShader;
		std::shared_ptr<IShader> _shadowMapShader = nullptr;
		std::string _name;
		std::string _footstepSoundPath;
		std::string _footstepSurfaceMapPath;
		std::vector<std::string> _footstepSurfaceSounds;


		BlendState _blendState = BlendState::Opaque;
		CullingMode _cullMode = CullingMode::BackFace;
		DepthBufferState _depthState = DepthBufferState::DepthDefault;
		float _cullDistance = 0.0f;

		BlendState _previousBlendState = BlendState::Opaque;
		CullingMode _previousCullMode = CullingMode::BackFace;
		DepthBufferState _previousDepthState = DepthBufferState::DepthDefault;
		MaterialFormat _format = MaterialFormat::None;

		std::map<std::string, std::string> _soundTags;
		bool _affectsGI = true;
		bool _emissiveAffectsGI = false;

		std::recursive_mutex _lock;
		uint32_t _objectFlags = 0;
		std::atomic<int32_t> _editorOpenCount = 0;
	};
}
