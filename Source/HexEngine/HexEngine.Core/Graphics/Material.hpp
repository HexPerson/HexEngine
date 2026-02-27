

#pragma once

#include "ITexture2D.hpp"
#include "ITexture3D.hpp"
#include "IShader.hpp"
#include "RenderStructs.hpp"
#include "../FileSystem/IResource.hpp"
#include "../FileSystem/JsonFile.hpp"

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
				_cullMode == other._cullMode
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

		std::shared_ptr<ITexture2D> GetTexture(MaterialTexture type) const;

		void SetVolumeTexture(ITexture3D* texture);
		ITexture3D* GetVolumeTexture() const { return _volumeTexture; }

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

		bool DoesHaveAnyReflectivity();

	public:
		MaterialProperties _properties;

	private:
		uint32_t _materialId = 0;
		std::shared_ptr<ITexture2D> _textures[MaterialTexture::Count];
		ITexture3D* _volumeTexture = nullptr;
		std::shared_ptr<IShader> _standardShader;
		std::shared_ptr<IShader> _shadowMapShader = nullptr;
		std::string _name;


		BlendState _blendState = BlendState::Opaque;
		CullingMode _cullMode = CullingMode::BackFace;
		DepthBufferState _depthState = DepthBufferState::DepthDefault;
		float _cullDistance = 0.0f;

		BlendState _previousBlendState = BlendState::Opaque;
		CullingMode _previousCullMode = CullingMode::BackFace;
		DepthBufferState _previousDepthState = DepthBufferState::DepthDefault;
		MaterialFormat _format = MaterialFormat::None;

		std::map<std::string, std::string> _soundTags;

		std::recursive_mutex _lock;
	};
}
