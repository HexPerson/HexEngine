

#pragma once

#include "../Required.hpp"
#include "IVertexBuffer.hpp"

namespace HexEngine
{
	class Mesh;
	class ITexture2D;
	class TextureAtlas;

	struct UIInstanceData
	{
		math::Vector2 center;
		math::Vector2 scale;
		math::Vector2 texcoord0;
		math::Vector2 texcoord1;
		math::Vector4 colourt;
		math::Vector4 colourb;
		math::Matrix rotation;
	};

	typedef uint32_t MeshInstanceId;

	class HEX_API UIInstance
	{
	public:
		friend class MeshInstanceManager;

		~UIInstance();

		void Start();
		void Finish();
		void Render(ITexture2D* texture, const math::Vector2& center, const math::Vector2& scale, const math::Vector2& texcoord0, const math::Vector2& texcoord1, const math::Vector4& colour, const math::Matrix& rotation);
		void Render(ITexture2D* texture, const math::Vector2& center, const math::Vector2& scale, const math::Vector2& texcoord0, const math::Vector2& texcoord1, const math::Vector4& colourt, const math::Vector4& colourb, const math::Matrix& rotation);
		void ResolveUvFromAtlas(TextureAtlas* atlas);
		int32_t GetSize() { return _poolIndex; }
		bool HasStarted() const;
		IVertexBuffer* GetBuffer() const;

	private:
		IVertexBuffer* _instanceBuffer = nullptr;
		std::vector<UIInstanceData> _data;
		std::vector<std::pair<size_t, ITexture2D*>> _textureToInstanceIdMap;
		uint32_t _instanceBufferNumElements = 0;
		uint32_t _poolIndex = 0;
		bool _hasStarted = false;
		bool _hasSetVertexData = false;
	};
}
