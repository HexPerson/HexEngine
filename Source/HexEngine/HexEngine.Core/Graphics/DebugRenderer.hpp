

#pragma once

#include "IVertexBuffer.hpp"
#include "IInputLayout.hpp"
#include "IIndexBuffer.hpp"
#include "IShader.hpp"

namespace HexEngine
{
	struct DebugVertex
	{
		math::Vector3 position;	
		math::Color colour;
	};

	class HEX_API DebugRenderer
	{
	public:
		struct DebugLines
		{
			math::Vector3 points[2];
			math::Color colour;
		};
		virtual ~DebugRenderer();

		void Create();

		void Destroy();

		void DrawFrustum(const dx::BoundingFrustum& frustum, const math::Color& colour);

		void DrawAABB(const dx::BoundingBox& bbox, const math::Color& colour = math::Color(1.0f, 0.0f, 0.5f, 0.4f));

		void DrawOBB(const dx::BoundingOrientedBox& bbox, const math::Color& colour = math::Color(0.0f, 1.0f, 0.0f, 0.4f));

		void DrawLine(const math::Vector3& from, const math::Vector3& to, const math::Color& colour = math::Color(1.0f, 0.0f, 0.5f, 0.4f));

		void DrawLines(const std::vector<std::pair<math::Vector3, math::Color>>& vertices);

		void DrawPolygon(const std::vector<std::pair<math::Vector3, math::Color>>& vertices);

		void FlushBuffers();

	private:
		IVertexBuffer* _lineVBuffer = nullptr;
		IIndexBuffer* _lineIBuffer = nullptr;

		IVertexBuffer* _polyVBuffer = nullptr;
		IIndexBuffer* _polyIBuffer = nullptr;

		std::shared_ptr<IShader> _debugShader;
		//IInputLayout* _inputLayout = nullptr;

		std::vector<DebugLines> _lines;
		uint32_t _numLineVertices = 0;

		std::vector<DebugVertex> _vertices;
		std::vector<uint32_t> _indices;
		uint32_t _numVertices = 0;
		uint32_t _vertIndex = 0;
	};
}
