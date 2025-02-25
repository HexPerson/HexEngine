

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
			64 * sizeof(MeshIndexFormat),
			sizeof(MeshIndexFormat),
			D3D11_USAGE_DYNAMIC,
			D3D11_CPU_ACCESS_WRITE);

		_polyVBuffer = g_pEnv->_graphicsDevice->CreateVertexBuffer(
			sizeof(DebugVertex) * 64,
			sizeof(DebugVertex),
			D3D11_USAGE_DYNAMIC,
			D3D11_CPU_ACCESS_WRITE);

		_polyIBuffer = g_pEnv->_graphicsDevice->CreateIndexBuffer(
			64 * sizeof(MeshIndexFormat),
			sizeof(MeshIndexFormat),
			D3D11_USAGE_DYNAMIC,
			D3D11_CPU_ACCESS_WRITE);

		_debugShader = IShader::Create("EngineData.Shaders/DebugRender.hcs");

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
		SAFE_DELETE(_lineIBuffer);
	}	

	void DebugRenderer::DrawPolygon(const std::vector<std::pair<math::Vector3, math::Color>>& vertices)
	{
		for (auto& v : vertices)
		{
			DebugVertex dv;
			dv.position = v.first;
			dv.colour = v.second;

			_vertices.push_back(dv);

			_indices.push_back(_vertIndex++);
			_indices.push_back(_vertIndex++);
			_indices.push_back(_vertIndex++);
		}
	}

	void DebugRenderer::DrawLines(const std::vector<std::pair<math::Vector3, math::Color>>& vertices)
	{
		for (auto i = 0; i < vertices.size(); i += 2)
		{
			DebugLines dl;
			dl.points[0] = vertices[i].first;
			dl.points[1] = vertices[i+1].first;
			dl.colour = vertices[i].second;

			_lines.push_back(dl);
		}
	}

	void DebugRenderer::DrawFrustum(const dx::BoundingFrustum& frustum, const math::Color& colour)
	{
		std::vector<DebugVertex> vtx(16);

		math::Vector3 corners[8], originalCorners[8];
		frustum.GetCorners(corners);

		memcpy(originalCorners, corners, sizeof(corners));

		DebugLines line;
		line.points[0] = corners[0];
		line.points[1] = corners[4];
		line.colour = colour;
		_lines.push_back(line);

		line.points[0] = corners[1];
		line.points[1] = corners[5];
		line.colour = colour;
		_lines.push_back(line);

		line.points[0] = corners[2];
		line.points[1] = corners[6];
		line.colour = colour;
		_lines.push_back(line);

		line.points[0] = corners[3];
		line.points[1] = corners[7];
		line.colour = colour;
		_lines.push_back(line);

		line.points[0] = corners[0];
		line.points[1] = corners[1];
		line.colour = colour;
		_lines.push_back(line);

		line.points[0] = corners[1];
		line.points[1] = corners[2];
		line.colour = colour;
		_lines.push_back(line);

		line.points[0] = corners[2];
		line.points[1] = corners[3];
		line.colour = colour;
		_lines.push_back(line);

		line.points[0] = corners[3];
		line.points[1] = corners[0];
		line.colour = colour;
		_lines.push_back(line);

		line.points[0] = corners[4];
		line.points[1] = corners[5];
		line.colour = colour;
		_lines.push_back(line);

		line.points[0] = corners[5];
		line.points[1] = corners[3];
		line.colour = colour;
		_lines.push_back(line);

		line.points[0] = corners[6];
		line.points[1] = corners[7];
		line.colour = colour;
		_lines.push_back(line);

		line.points[0] = corners[7];
		line.points[1] = corners[4];
		line.colour = colour;
		_lines.push_back(line);
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
		auto graphicsDevice = g_pEnv->_graphicsDevice;

		if (_lines.size() > 0)
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

		if (_vertices.size() > 0)
		{
			if (_vertices.size() > _numVertices || !_polyVBuffer)
			{
				SAFE_DELETE(_polyVBuffer);

				_polyVBuffer = g_pEnv->_graphicsDevice->CreateVertexBuffer(
					sizeof(DebugVertex) * _vertices.size(),
					sizeof(DebugVertex),
					D3D11_USAGE_DYNAMIC,
					D3D11_CPU_ACCESS_WRITE);

				_numVertices = _vertices.size();

				SAFE_DELETE(_polyIBuffer);

				_polyIBuffer = g_pEnv->_graphicsDevice->CreateIndexBuffer(
					_indices.size() * sizeof(MeshIndexFormat),
					sizeof(MeshIndexFormat),
					D3D11_USAGE_DYNAMIC,
					D3D11_CPU_ACCESS_WRITE);

				//_numLineVertices = idx.size();
			}

			_polyVBuffer->SetVertexData((uint8_t*)_vertices.data(), (uint32_t)_vertices.size() * sizeof(DebugVertex));
			//_polyIBuffer->SetIndexData((uint8_t*)_indices.data(), _indices.size() * sizeof(MeshIndexFormat));

			graphicsDevice->SetVertexBuffer(0, _polyVBuffer);
			graphicsDevice->SetIndexBuffer(_polyIBuffer);
			graphicsDevice->SetTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			graphicsDevice->SetPixelShader(_debugShader->GetShaderStage(ShaderStage::PixelShader));
			graphicsDevice->SetVertexShader(_debugShader->GetShaderStage(ShaderStage::VertexShader));
			graphicsDevice->SetInputLayout(_debugShader->GetInputLayout());
			graphicsDevice->Draw(_vertices.size(), 0);

			_vertices.clear();
			_indices.clear();
			_vertIndex = 0;
		}
	}
}