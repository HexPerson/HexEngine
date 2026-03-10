
#include "DebugRender.hpp"

RCDebugRenderer::~RCDebugRenderer()
{

}

void RCDebugRenderer::depthMask(bool state)
{

}

void RCDebugRenderer::texture(bool state)
{

}

/// Begin drawing primitives.
///  @param prim [in] primitive type to draw, one of rcDebugDrawPrimitives.
///  @param size [in] size of a primitive, applies to point size and line width only.
void RCDebugRenderer::begin(duDebugDrawPrimitives prim, float size)
{
	_type = prim;

	if (prim == duDebugDrawPrimitives::DU_DRAW_LINES || prim == duDebugDrawPrimitives::DU_DRAW_TRIS)
		_doDraw = true;
	else
		_doDraw = false;
}

/// Submit a vertex
///  @param pos [in] position of the verts.
///  @param color [in] color of the verts.
void RCDebugRenderer::vertex(const float* pos, unsigned int color)
{
	if (_doDraw == false)
		return;

	_polys.push_back({ math::Vector3(pos), math::Color(color) });
}

/// Submit a vertex
///  @param x,y,z [in] position of the verts.
///  @param color [in] color of the verts.
void RCDebugRenderer::vertex(const float x, const float y, const float z, unsigned int color)
{
	if (_doDraw == false)
		return;
	
	_polys.push_back({ math::Vector3(x, y, z), math::Color(color) });
}

/// Submit a vertex
///  @param pos [in] position of the verts.
///  @param color [in] color of the verts.
///  @param uv [in] the uv coordinates of the verts.
void RCDebugRenderer::vertex(const float* pos, unsigned int color, const float* uv)
{
	if (_doDraw == false)
		return;

	_polys.push_back({ math::Vector3(pos), math::Color(color) });
}

/// Submit a vertex
///  @param x,y,z [in] position of the verts.
///  @param color [in] color of the verts.
///  @param u,v [in] the uv coordinates of the verts.
void RCDebugRenderer::vertex(const float x, const float y, const float z, unsigned int color, const float u, const float v)
{
	if (_doDraw == false)
		return;

	_polys.push_back({ math::Vector3(x, y, z), math::Color(color) });
}

/// End drawing primitives.
void RCDebugRenderer::end()
{
	switch (_type)
	{
	case duDebugDrawPrimitives::DU_DRAW_LINES:
		HexEngine::g_pEnv->_debugRenderer->DrawLines(_polys);
		break;

	case duDebugDrawPrimitives::DU_DRAW_TRIS:
		HexEngine::g_pEnv->_debugRenderer->DrawPolygon(_polys);
		break;
	}
	_polys.clear();
}