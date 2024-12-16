

#include "DebugRenderer.hpp"
#include "../HexEngine.hpp"

namespace HexEngine
{
	void DebugRenderer::Create()
	{
		_lineVBuffer = g_pEnv->_graphicsDevice->CreateVertexBuffer(
			sizeof(DebugVertex) * 64,
			sizeof(DebugVertex),
			D3D11_USAGE_DYNAMIC,
			D3D11_CPU_ACCESS_WRITE);

		_lineIBuffer = g_pEnv->_graphicsDevice->CreateIndexBuffer(
			64 * sizeof(uint32_t),
			sizeof(uint32_t),
			D3D11_USAGE_DYNAMIC,
			D3D11_CPU_ACCESS_WRITE);

		_debugShader = (IShader*)g_pEnv->_resourceSystem->LoadResource("EngineData.Shaders/DebugRender.hcs");

		//D3D11_INPUT_ELEMENT_DESC inputDesc[] = {
		//	{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
		//	{"COLOUR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
		//};

		//// Create the input layout
		//std::vector<uint8_t> vertexShaderCode;
		//if (_debugShader->GetShaderStage(ShaderStage::VertexShader)->GetBinaryCode(vertexShaderCode))
		//{
		//	_inputLayout = g_pEnv->_graphicsDevice->CreateInputLayout(inputDesc, _countof(inputDesc), vertexShaderCode);
		//}

		
	}

	DebugRenderer::~DebugRenderer()
	{
		Destroy();
	}

	void DebugRenderer::Destroy()
	{
		SAFE_DELETE(_lineVBuffer);
		//SAFE_DELETE(_inputLayout);
		SAFE_UNLOAD(_debugShader);
	}	

	void DebugRenderer::DrawFrustum(const dx::BoundingFrustum& frustum)
	{
		std::vector<DebugVertex> vtx(16);

		math::Vector3 corners[8], originalCorners[8];
		frustum.GetCorners(corners);

		memcpy(originalCorners, corners, sizeof(corners));

		vtx[0].position = corners[0];
		vtx[1].position = corners[4];

		vtx[2].position = corners[1];
		vtx[3].position = corners[5];

		vtx[4].position = corners[2];
		vtx[5].position = corners[6];

		vtx[6].position = corners[3];
		vtx[7].position = corners[7];

		// far joins
		vtx[8].position = corners[0];
		vtx[9].position = corners[1];

		vtx[10].position = corners[1];
		vtx[11].position = corners[2];

		vtx[12].position = corners[2];
		vtx[13].position = corners[3];

		vtx[14].position = corners[3];
		vtx[15].position = corners[0];

		// near joins
		/*vtx[16].position = corners[4];
		vtx[17].position = corners[5];

		vtx[18].position = corners[5];
		vtx[19].position = corners[6];

		vtx[20].position = corners[6];
		vtx[21].position = corners[7];

		vtx[22].position = corners[7];
		vtx[23].position = corners[4];*/

		//auto splitCornerNear = [](math::Vector3* corners, const math::Vector3* original, float distance, int index) {

		//	auto dir = (original[index - 4] - original[index]);
		//	float len = dir.Length();
		//	dir.Normalize();

		//	corners[index] += dir * len * distance;
		//};
		//auto splitCornerFar = [](math::Vector3* corners, const math::Vector3* original, float distance, int index) {

		//	auto dir = (original[index] - original[index + 4]);
		//	float len = dir.Length();
		//	dir.Normalize();

		//	corners[index] = original[index + 4] + (dir * len * (distance));
		//};
		//splitCornerFar(corners, originalCorners, 0.6f, 0);
		//splitCornerFar(corners, originalCorners, 0.6f, 1);
		//splitCornerFar(corners, originalCorners, 0.6f, 2);
		//splitCornerFar(corners, originalCorners, 0.6f, 3);

		//splitCornerNear(corners, originalCorners, 0.15f, 4);
		//splitCornerNear(corners, originalCorners, 0.15f, 5);
		//splitCornerNear(corners, originalCorners, 0.15f, 6);
		//splitCornerNear(corners, originalCorners, 0.15f, 7);

		//// near
		//vtx[24].position = corners[4];
		//vtx[25].position = corners[5];

		//vtx[26].position = corners[5];
		//vtx[27].position = corners[6];

		//vtx[28].position = corners[6];
		//vtx[29].position = corners[7];

		//vtx[30].position = corners[7];
		//vtx[31].position = corners[4];

		//// far
		//vtx[32].position = corners[0];
		//vtx[33].position = corners[1];

		//vtx[34].position = corners[1];
		//vtx[35].position = corners[2];

		//vtx[36].position = corners[2];
		//vtx[37].position = corners[3];

		//vtx[38].position = corners[3];
		//vtx[39].position = corners[0];

		auto graphicsDevice = g_pEnv->_graphicsDevice;

		auto perObjectBuffer = graphicsDevice->GetEngineConstantBuffer(EngineConstantBuffer::PerObjectBuffer);

		if (perObjectBuffer)
		{
			PerObjectBuffer bufferData;
			bufferData._worldMatrix = math::Matrix::Identity.Transpose();

			bufferData._material.diffuseColour = math::Vector4(1.0f, 0.0f, 0.5f, 0.4f);

			perObjectBuffer->Write(&bufferData, sizeof(bufferData));
		}

		_lineVBuffer->SetVertexData((uint8_t*)vtx.data(), (uint32_t)vtx.size() * sizeof(DebugVertex));

		graphicsDevice->SetVertexBuffer(0, _lineVBuffer);
		graphicsDevice->SetTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
		graphicsDevice->SetPixelShader(_debugShader->GetShaderStage(ShaderStage::PixelShader));
		graphicsDevice->SetVertexShader(_debugShader->GetShaderStage(ShaderStage::VertexShader));
		graphicsDevice->SetInputLayout(_debugShader->GetInputLayout());
		graphicsDevice->Draw((uint32_t)vtx.size());
	}

	void DebugRenderer::DrawAABB(const dx::BoundingBox& bbox, const math::Color& colour)
	{
		std::vector<DebugVertex> vtx(24);

		math::Vector3 corners[8];
		bbox.GetCorners(corners);

		const math::Color col(0, 1, 0, 0.5f);

		DebugLines line;
		line.points[0] = corners[2];
		line.points[1] = corners[6];
		line.colour = col;
		_lines.push_back(line);

		line.points[0] = corners[6];
		line.points[1] = corners[7];
		line.colour = colour;
		_lines.push_back(line);

		line.points[0] = corners[7];
		line.points[1] = corners[3];
		line.colour = colour;
		_lines.push_back(line);

		line.points[0] = corners[3];
		line.points[1] = corners[2];
		line.colour = colour;
		_lines.push_back(line);

		// far joins
		line.points[0] = corners[0];
		line.points[1] = corners[4];
		line.colour = colour;
		_lines.push_back(line);

		line.points[0] = corners[4];
		line.points[1] = corners[5];
		line.colour = colour;
		_lines.push_back(line);

		line.points[0] = corners[5];
		line.points[1] = corners[1];
		line.colour = colour;
		_lines.push_back(line);

		line.points[0] = corners[1];
		line.points[1] = corners[0];
		line.colour = colour;
		_lines.push_back(line);

		// connect top and bottom
		line.points[0] = corners[2];
		line.points[1] = corners[1];
		line.colour = colour;
		_lines.push_back(line);

		line.points[0] = corners[6];
		line.points[1] = corners[5];
		line.colour = colour;
		_lines.push_back(line);

		line.points[0] = corners[7];
		line.points[1] = corners[4];
		line.colour = colour;
		_lines.push_back(line);

		line.points[0] = corners[3];
		line.points[1] = corners[0];
		line.colour = colour;
		_lines.push_back(line);
	}

	void DebugRenderer::DrawLine(const math::Vector3& from, const math::Vector3& to, const math::Color& colour)
	{
		if ((to - from).Length() <= FLT_EPSILON)
			return;

		DebugLines line;
		line.points[0] = from;
		line.points[1] = to;
		line.colour = colour;

		_lines.push_back(line);

		
	}

	void DebugRenderer::DrawOBB(const dx::BoundingOrientedBox& bbox, const math::Color& colour)
	{
		std::vector<DebugVertex> vtx(24);

		math::Vector3 corners[8];
		bbox.GetCorners(corners);

		const math::Color col(0, 1, 0, 0.5f);

		DebugLines line;
		line.points[0] = corners[2];
		line.points[1] = corners[6];
		line.colour = col;
		_lines.push_back(line);

		line.points[0] = corners[6];
		line.points[1] = corners[7];
		line.colour = colour;
		_lines.push_back(line);

		line.points[0] = corners[7];
		line.points[1] = corners[3];
		line.colour = colour;
		_lines.push_back(line);

		line.points[0] = corners[3];
		line.points[1] = corners[2];
		line.colour = colour;
		_lines.push_back(line);

		// far joins
		line.points[0] = corners[0];
		line.points[1] = corners[4];
		line.colour = colour;
		_lines.push_back(line);

		line.points[0] = corners[4];
		line.points[1] = corners[5];
		line.colour = colour;
		_lines.push_back(line);

		line.points[0] = corners[5];
		line.points[1] = corners[1];
		line.colour = colour;
		_lines.push_back(line);

		line.points[0] = corners[1];
		line.points[1] = corners[0];
		line.colour = colour;
		_lines.push_back(line);

		// connect top and bottom
		line.points[0] = corners[2];
		line.points[1] = corners[1];
		line.colour = colour;
		_lines.push_back(line);

		line.points[0] = corners[6];
		line.points[1] = corners[5];
		line.colour = colour;
		_lines.push_back(line);

		line.points[0] = corners[7];
		line.points[1] = corners[4];
		line.colour = colour;
		_lines.push_back(line);

		line.points[0] = corners[3];
		line.points[1] = corners[0];
		line.colour = colour;
		_lines.push_back(line);

		/*auto graphicsDevice = g_pEnv->_graphicsDevice;

		auto perObjectBuffer = graphicsDevice->GetEngineConstantBuffer(EngineConstantBuffer::PerObjectBuffer);

		if (perObjectBuffer)
		{
			PerObjectBuffer bufferData;
			bufferData._worldMatrix = math::Matrix::Identity.Transpose();
			bufferData._material.diffuseColour = math::Vector4(1.0f, 0.0f, 0.5f, 0.4f);;

			perObjectBuffer->Write(&bufferData, sizeof(bufferData));
		}

		_lineVBuffer->SetVertexData((uint8_t*)vtx.data(), (uint32_t)vtx.size() * sizeof(DebugVertex));

		graphicsDevice->SetVertexBuffer(0, _lineVBuffer);
		graphicsDevice->SetTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
		graphicsDevice->SetPixelShader(_debugShader->GetShaderStage(ShaderStage::PixelShader));
		graphicsDevice->SetVertexShader(_debugShader->GetShaderStage(ShaderStage::VertexShader));
		graphicsDevice->SetInputLayout(_inputLayout);
		graphicsDevice->Draw((uint32_t)vtx.size());*/

		//DrawLine()
	}

	void DebugRenderer::FlushBuffers()
	{
		std::vector<DebugVertex> vtx;
		std::vector<MeshIndexFormat> idx;

		uint32_t i = 0;

		for (auto&& line : _lines)
		{
			DebugVertex v;
			v.position = line.points[0];
			v.colour = line.colour;
			vtx.push_back(v);

			v.position = line.points[1];
			v.colour = line.colour;
			vtx.push_back(v);			

			idx.push_back(i + 0);
			idx.push_back(i + 1);

			i += 2;
		}

		if (vtx.size() > _numLineVertices || !_lineVBuffer)
		{
			SAFE_DELETE(_lineVBuffer);

			_lineVBuffer = g_pEnv->_graphicsDevice->CreateVertexBuffer(
				sizeof(DebugVertex) * vtx.size(),
				sizeof(DebugVertex),
				D3D11_USAGE_DYNAMIC,
				D3D11_CPU_ACCESS_WRITE);

			_numLineVertices = vtx.size();

			SAFE_DELETE(_lineIBuffer);

			_lineIBuffer = g_pEnv->_graphicsDevice->CreateIndexBuffer(
				idx.size() * sizeof(MeshIndexFormat),
				sizeof(MeshIndexFormat),
				D3D11_USAGE_DYNAMIC,
				D3D11_CPU_ACCESS_WRITE);

			//_numLineVertices = idx.size();
		}
		

		auto graphicsDevice = g_pEnv->_graphicsDevice;

		auto perObjectBuffer = graphicsDevice->GetEngineConstantBuffer(EngineConstantBuffer::PerObjectBuffer);

		if (perObjectBuffer)
		{
			PerObjectBuffer bufferData = {};
			bufferData._worldMatrix = math::Matrix::Identity.Transpose();
			bufferData._material.diffuseColour = math::Vector4(0, 0, 1, 0.5f);// colour.ToVector4();// math::Vector4(1.0f, 0.0f, 0.5f, 0.4f);;

			perObjectBuffer->Write(&bufferData, sizeof(bufferData));
		}

		_lineVBuffer->SetVertexData((uint8_t*)vtx.data(), (uint32_t)vtx.size() * sizeof(DebugVertex));
		_lineIBuffer->SetIndexData((uint8_t*)idx.data(), idx.size() * sizeof(MeshIndexFormat));

		graphicsDevice->SetVertexBuffer(0, _lineVBuffer);
		graphicsDevice->SetIndexBuffer(_lineIBuffer);
		graphicsDevice->SetTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
		graphicsDevice->SetPixelShader(_debugShader->GetShaderStage(ShaderStage::PixelShader));
		graphicsDevice->SetVertexShader(_debugShader->GetShaderStage(ShaderStage::VertexShader));
		graphicsDevice->SetInputLayout(_debugShader->GetInputLayout());
		graphicsDevice->DrawIndexed((uint32_t)idx.size());

		_lines.clear();
	}
}