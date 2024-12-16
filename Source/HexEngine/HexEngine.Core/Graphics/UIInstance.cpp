

#include "UIInstance.hpp"
#include "../Environment/IEnvironment.hpp"
#include "IGraphicsDevice.hpp"
#include "TextureAtlas.hpp"

namespace HexEngine
{
	void UIInstance::Start()
	{
		_hasStarted = true;
		_hasSetVertexData = false;
		_poolIndex = 0;
		_textureToInstanceIdMap.clear();
	}

	UIInstance::~UIInstance()
	{
		SAFE_DELETE(_instanceBuffer);
	}

	bool UIInstance::HasStarted() const
	{
		return _hasStarted;
	}

	IVertexBuffer* UIInstance::GetBuffer() const
	{
		return _instanceBuffer;
	}

	void UIInstance::ResolveUvFromAtlas(TextureAtlas* atlas)
	{
		auto totalWidth = atlas->GetTotalWidth();
		auto totalHeight = atlas->GetTotalHeight();

		for (auto& instance : _textureToInstanceIdMap)
		{
			auto poolId = instance.first;
			auto texture = instance.second;

			auto atlasItem = atlas->GetTexureItemFromAtlas(texture);

			if (!atlasItem)
				continue;

			auto& pool = _data.at(poolId);

			// scale the width and height
			float scaledWidth = ((float)texture->GetWidth() - 1.0f) / (float)totalWidth;
			float scaledHeight = ((float)texture->GetHeight() - 1.0f) / (float)totalHeight;

			// original x and y coords
			float origX0 = (float)texture->GetWidth() * pool.texcoord0.x;
			float origY0 = (float)texture->GetHeight() * pool.texcoord0.y;

			float origX1 = (float)texture->GetWidth() * pool.texcoord1.x;
			float origY1 = (float)texture->GetHeight() * pool.texcoord1.y;

			origX0 += atlasItem->x;
			origY0 += atlasItem->y;

			origX1 += atlasItem->x;
			origY1 += atlasItem->y;

			pool.texcoord0.x = origX0 / (float)totalWidth;
			pool.texcoord0.y = origY0 / (float)totalHeight;

			pool.texcoord1.x = origX1 / (float)totalWidth;
			pool.texcoord1.y = origY1 / (float)totalHeight;
		}
	}

	void UIInstance::Render(ITexture2D* texture, const math::Vector2& center, const math::Vector2& scale, const math::Vector2& texcoord0, const math::Vector2& texcoord1, const math::Vector4& colour, const math::Matrix& rotation)
	{
		if ((_poolIndex + 1) > (int32_t)_data.size())
		{
			UIInstanceData data;
			data.center = center;
			data.scale = scale;
			data.texcoord0 = texcoord0;
			data.texcoord1 = texcoord1;
			data.colourt = data.colourb = colour;
			data.rotation = rotation;

			_data.push_back(data);
		}
		else
		{
			UIInstanceData& data = _data.at(_poolIndex);

			data.center = center;
			data.scale = scale;
			data.texcoord0 = texcoord0;
			data.texcoord1 = texcoord1;
			data.colourt = data.colourb = colour;
			data.rotation = rotation;
		}

		_textureToInstanceIdMap.push_back({ _poolIndex, texture });

		_poolIndex++;
	}

	void UIInstance::Render(ITexture2D* texture, const math::Vector2& center, const math::Vector2& scale, const math::Vector2& texcoord0, const math::Vector2& texcoord1, const math::Vector4& colourt, const math::Vector4& colourb, const math::Matrix& rotation)
	{
		if ((_poolIndex + 1) > (int32_t)_data.size())
		{
			UIInstanceData data;
			data.center = center;
			data.scale = scale;
			data.texcoord0 = texcoord0;
			data.texcoord1 = texcoord1;
			data.colourt = colourt;
			data.colourb = colourb;
			data.rotation = rotation;

			_data.push_back(data);
		}
		else
		{
			UIInstanceData& data = _data.at(_poolIndex);

			data.center = center;
			data.scale = scale;
			data.texcoord0 = texcoord0;
			data.texcoord1 = texcoord1;
			data.colourt = colourt;
			data.colourb = colourb;
			data.rotation = rotation;
		}

		_poolIndex++;

		//_currentInstanceId++;
	}

	void UIInstance::Finish()
	{
		if (_poolIndex == 0)
			return;

		// if we have too many instances, we have to rebuild the buffer
		//
		if (_poolIndex > _instanceBufferNumElements || _instanceBuffer == nullptr)
		{
			SAFE_DELETE(_instanceBuffer);

			_instanceBuffer = g_pEnv->_graphicsDevice->CreateVertexBuffer(
				sizeof(UIInstanceData) * _poolIndex,
				sizeof(UIInstanceData),
				D3D11_USAGE_DYNAMIC,
				D3D11_CPU_ACCESS_WRITE);

			_instanceBufferNumElements = _poolIndex;
		}

		// finally set the buffer data
		//
		if (_hasSetVertexData == false)
		{
			_instanceBuffer->SetVertexData((uint8_t*)_data.data(), _poolIndex * sizeof(UIInstanceData));
			_hasSetVertexData = true;
		}

		// then set the instance buffer to the pipeline
		//
		g_pEnv->_graphicsDevice->SetVertexBuffer(1, _instanceBuffer);

		_hasStarted = false;
	}
}