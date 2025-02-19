
#pragma once

#include <HexEngine.Core/HexEngine.hpp>
#include "DebugUtils/Include/DebugDraw.h"

class RCDebugRenderer : public duDebugDraw
{
public:
	virtual ~RCDebugRenderer();

	virtual void depthMask(bool state) override;

	virtual void texture(bool state) override;

	/// Begin drawing primitives.
	///  @param prim [in] primitive type to draw, one of rcDebugDrawPrimitives.
	///  @param size [in] size of a primitive, applies to point size and line width only.
	virtual void begin(duDebugDrawPrimitives prim, float size = 1.0f) override;

	/// Submit a vertex
	///  @param pos [in] position of the verts.
	///  @param color [in] color of the verts.
	virtual void vertex(const float* pos, unsigned int color) override;

	/// Submit a vertex
	///  @param x,y,z [in] position of the verts.
	///  @param color [in] color of the verts.
	virtual void vertex(const float x, const float y, const float z, unsigned int color) override;

	/// Submit a vertex
	///  @param pos [in] position of the verts.
	///  @param color [in] color of the verts.
	///  @param uv [in] the uv coordinates of the verts.
	virtual void vertex(const float* pos, unsigned int color, const float* uv) override;

	/// Submit a vertex
	///  @param x,y,z [in] position of the verts.
	///  @param color [in] color of the verts.
	///  @param u,v [in] the uv coordinates of the verts.
	virtual void vertex(const float x, const float y, const float z, unsigned int color, const float u, const float v) override;

	/// End drawing primitives.
	virtual void end() override;

private:
	duDebugDrawPrimitives _type;
	std::vector<std::pair<math::Vector3, math::Color>> _polys;
	bool _doDraw = false;
};
