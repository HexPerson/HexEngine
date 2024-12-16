

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

	class DebugRenderer
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

		void DrawFrustum(const dx::BoundingFrustum& frustum);

		void DrawAABB(const dx::BoundingBox& bbox, const math::Color& colour = math::Color(1.0f, 0.0f, 0.5f, 0.4f));

		void DrawOBB(const dx::BoundingOrientedBox& bbox, const math::Color& colour = math::Color(0.0f, 1.0f, 0.0f, 0.4f));

		void DrawLine(const math::Vector3& from, const math::Vector3& to, const math::Color& colour = math::Color(1.0f, 0.0f, 0.5f, 0.4f));

		void FlushBuffers();

	private:
		IVertexBuffer* _lineVBuffer = nullptr;
		IIndexBuffer* _lineIBuffer = nullptr;
		IShader* _debugShader = nullptr;
		//IInputLayout* _inputLayout = nullptr;

		std::vector<DebugLines> _lines;
		uint32_t _numLineVertices = 0;
	};
}
