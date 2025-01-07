

#include "Material.hpp"
#include "../HexEngine.hpp"

namespace HexEngine
{
	static uint32_t sMaterialCount = 0;

	Material::Material() :
		_materialId(++sMaterialCount)
	{}

	Material::~Material()
	{
		Destroy();
	}

	void Material::SetName(const std::string& name)
	{
		_name = name;
	}

	const std::string& Material::GetName() const
	{
		return _name;
	}

	void Material::Destroy()
	{
	}

	std::shared_ptr<Material> Material::Create(const fs::path& path)
	{
		return dynamic_pointer_cast<Material>(g_pEnv->_resourceSystem->LoadResource(path));
	}

	bool Material::Exists(const fs::path& path)
	{
		return g_pEnv->_resourceSystem->DoesResourceExistAsAsset(path);
	}

	void Material::CopyFrom(const std::shared_ptr<Material>& material)
	{
		CopyFrom(*material.get());
	}

	void Material::CopyFrom(const Material& material)
	{
		std::unique_lock lock(_lock);

		for (auto i = 0; i < MaterialTexture::Count; ++i)
		{
			SetTexture((MaterialTexture)i, material._textures[i]);
		}
		SetStandardShader(material._standardShader);
		SetShadowMapShader(material._shadowMapShader);

		_properties = material._properties;
		_standardShader = material._standardShader;
		_shadowMapShader = material._shadowMapShader;
		_blendState = material._blendState;
		_depthState = material._depthState;
		_cullMode = material._cullMode;
	}

	Material::Material(const Material& other)
	{
		CopyFrom(other);
	}

	Material& Material::operator = (const Material& other)
	{
		CopyFrom(other);
		return *this;
	}

	Material& Material::operator = (const std::shared_ptr<Material>& other)
	{
		CopyFrom(other);
		return *this;
	}

	void Material::SetTexture(MaterialTexture type, const std::shared_ptr<ITexture2D>& texture)
	{
		if (!texture)
			return;

		std::unique_lock lock(_lock);

		auto oldTexture = _textures[type];

		_textures[type] = texture;
	}

	void Material::SetVolumeTexture(ITexture3D* texture)
	{
		std::unique_lock lock(_lock);

		_volumeTexture = texture;
	}

	void Material::SetStandardShader(const std::shared_ptr<IShader>& shader)
	{
		std::unique_lock lock(_lock);

		if (shader)
		{
			_standardShader = shader;
		}
	}

	void Material::SetShadowMapShader(const std::shared_ptr<IShader>& shader)
	{
		std::unique_lock lock(_lock);

		if (shader)
		{
			_shadowMapShader = shader;
		}
	}

	std::shared_ptr<IShader> Material::GetStandardShader() const
	{
		return _standardShader;
	}

	std::shared_ptr<IShader> Material::GetShadowMapShader() const
	{
		return _shadowMapShader;
	}

	std::shared_ptr<ITexture2D> Material::GetTexture(MaterialTexture type) const
	{
		return _textures[type];
	}

	std::shared_ptr<Material> Material::GetDefaultMaterial()
	{
		static auto defaultMat = dynamic_pointer_cast<Material>(g_pEnv->_resourceSystem->LoadResource("EngineData.Materials/Default.hmat"));
		return defaultMat;
	}

	/*void Material::Load(DiskFile* file)
	{
		auto path = file->ReadString();

		Material* mat = Create(path);
		CopyFrom(mat);
		SAFE_UNLOAD(mat);

		return;

		int32_t i = 0;

		for (auto tex : _textures)
		{
			if (file->Read<bool>())
			{
				std::string texPath = file->ReadString();

				LOG_DEBUG("Loading texture %s", texPath.c_str());

				SetTexture((MaterialTexture)i, ITexture2D::Create(texPath));
			}

			++i;
		}

		if (file->Read<bool>())
		{
			std::string shaderStr = file->ReadString();
			LOG_DEBUG("Loading shader %s", shaderStr.c_str());
			SetStandardShader((IShader*)g_pEnv->_resourceSystem->LoadResource(shaderStr));
		}

		if (file->Read<bool>())
		{
			std::string shaderStr = file->ReadString();
			LOG_DEBUG("Loading shader %s", shaderStr.c_str());
			SetShadowMapShader((IShader*)g_pEnv->_resourceSystem->LoadResource(shaderStr));
		}

		file->Read<MaterialProperties>(&_properties);
	}*/

	void Material::SetBlendState(BlendState state)
	{
		_blendState = state;
	}

	void Material::SetCullMode(CullingMode mode)
	{
		_cullMode = mode;
		_previousCullMode = mode;
	}

	void Material::SetDepthState(DepthBufferState state)
	{
		_depthState = state;
	}

	BlendState Material::GetBlendState() const
	{
		return _blendState;
	}

	CullingMode Material::GetCullMode() const
	{
		return _cullMode;
	}

	DepthBufferState Material::GetDepthState() const
	{
		return _depthState;
	}

	void Material::SaveRenderState()
	{
		auto graphicsDevice = g_pEnv->_graphicsDevice;

		_previousBlendState = graphicsDevice->GetBlendState();
		_previousDepthState = graphicsDevice->GetDepthBufferState();
		_previousCullMode = graphicsDevice->GetCullingMode();
	}

	void Material::RestoreRenderState()
	{
		auto graphicsDevice = g_pEnv->_graphicsDevice;

		graphicsDevice->SetBlendState(_previousBlendState);
		graphicsDevice->SetDepthBufferState(_previousDepthState);
		graphicsDevice->SetCullingMode(_previousCullMode);
	}

	void Material::AddSoundTag(const std::string& key, const std::string& value)
	{
		if (_soundTags.find(key) == _soundTags.end())
		{
			_soundTags[key] = value;
		}
	}

	const std::string& Material::GetSoundTag(const std::string& key) const
	{
		if (auto it = _soundTags.find(key); it != _soundTags.end())
		{
			return it->second;
		}
		return "";
	}

	void Material::Lock()
	{
		_lock.lock();
	}

	void Material::Unlock()
	{
		_lock.unlock();
	}

	const std::wstring& Material::GetMaterialTextureName(MaterialTexture type)
	{
		static const std::wstring sTextureTypeNames[MaterialTexture::Count] = {
			L"Albedo",
			L"Normal",
			L"Roughness",
			L"Metallic",
			L"Height",
			L"Emission",
			L"Opacity",
			L"Ambient Occlusion"
		};

		return sTextureTypeNames[type];
	}
}