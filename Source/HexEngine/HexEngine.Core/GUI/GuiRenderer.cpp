

#include "GuiRenderer.hpp"
#include "../HexEngine.hpp"

namespace HexEngine
{
	GuiRenderer::GuiRenderer()
	{
		_basicShader = IShader::Create("EngineData.Shaders/UIBasic.hcs");
		_instancedShader = IShader::Create("EngineData.Shaders/UIInstanced.hcs");

		CreateInstancedBuffers();

		_vertexBuffer = g_pEnv->_graphicsDevice->CreateVertexBuffer(
			4 * sizeof(GuiVertex),
			sizeof(GuiVertex),
			D3D11_USAGE_DYNAMIC,
			D3D11_CPU_ACCESS_WRITE);

		_indexBuffer = g_pEnv->_graphicsDevice->CreateIndexBuffer(
			6 * sizeof(MeshIndexFormat),
			sizeof(MeshIndexFormat),
			D3D11_USAGE_DYNAMIC,
			D3D11_CPU_ACCESS_WRITE);

		_inputLayout = IInputLayout::GetLayout_PosTexColour(_basicShader->GetShaderStage(ShaderStage::VertexShader));
		_instancedInputLayout = IInputLayout::GetLayout_UI_INSTANCED(_instancedShader->GetShaderStage(ShaderStage::VertexShader));

		_basicWhiteTex = ITexture2D::GetDefaultTexture();

		Style::CreateDefaultStyle(_style);
	}

	GuiRenderer::~GuiRenderer()
	{
		SAFE_UNLOAD(_basicShader);
		SAFE_UNLOAD(_instancedShader);

		SAFE_UNLOAD(_basicWhiteTex);

		SAFE_DELETE(_vertexBuffer);
		SAFE_DELETE(_indexBuffer);
		SAFE_DELETE(_fontVertexBuffer);
		SAFE_DELETE(_fontIndexBuffer);
		SAFE_DELETE(_instancedVertexBuffer);
		SAFE_DELETE(_instancedIndexBuffer);

		//SAFE_DELETE(_quadInstance);
	}

	bool GuiRenderer::CreateInstancedBuffers()
	{
		//_quadInstance = new UIInstance;

		uint32_t numQuadsToAlloc = 1;
		auto numQuads = numQuadsToAlloc;
		auto numTriangles = numQuads * 2;
		auto numVertices = numQuads * 4;
		auto numIndexes = numTriangles * 3;

		InstancedGuiVertex fontVertices[4];

		fontVertices[0].position = math::Vector4(-1, -1, 0, 1);
		fontVertices[1].position = math::Vector4(-1, 1, 0, 1);
		fontVertices[2].position = math::Vector4(1, 1, 0, 1);
		fontVertices[3].position = math::Vector4(1, -1, 0, 1);

		_instancedVertexBuffer = g_pEnv->_graphicsDevice->CreateVertexBuffer(
			(4 * sizeof(InstancedGuiVertex)),
			sizeof(InstancedGuiVertex),
			D3D11_USAGE_DEFAULT,
			0,
			fontVertices);

		std::vector<MeshIndexFormat> indices(numIndexes);

		for (UINT i = 0; i < numIndexes / 6; ++i)
		{
			indices[i * 6] = i * 4;
			indices[i * 6 + 1] = i * 4 + 1;
			indices[i * 6 + 2] = i * 4 + 2;
			indices[i * 6 + 3] = i * 4;
			indices[i * 6 + 4] = i * 4 + 2;
			indices[i * 6 + 5] = i * 4 + 3;
		}

		_instancedIndexBuffer = g_pEnv->_graphicsDevice->CreateIndexBuffer(
			numIndexes * sizeof(MeshIndexFormat),
			sizeof(MeshIndexFormat),
			D3D11_USAGE_DEFAULT,
			0,
			indices.data());

		return true;
	}

	void GuiRenderer::StartFrame(uint32_t width, uint32_t height)
	{
		auto gfxDevice = g_pEnv->_graphicsDevice;

		gfxDevice->SetVertexShader(_basicShader->GetShaderStage(ShaderStage::VertexShader));
		gfxDevice->SetPixelShader(_basicShader->GetShaderStage(ShaderStage::PixelShader));

		gfxDevice->SetInputLayout(_inputLayout);
		//gfxDevice->SetTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		gfxDevice->SetIndexBuffer(_indexBuffer);
		gfxDevice->SetVertexBuffer(0, _vertexBuffer);

		if (width == 0 && height == 0)
			gfxDevice->GetBackBufferDimensions(_screenWidth, _screenHeight);
		else
		{
			_screenWidth = width;
			_screenHeight = height;
		}

		gfxDevice->SetDepthBufferState(DepthBufferState::DepthNone);
		gfxDevice->SetCullingMode(CullingMode::NoCulling);
		
		//gfxDevice->SetBlendState(BlendState::Transparency);

		//_baseDrawList.Clear();
	}

	void GuiRenderer::EnableScaling(bool enable)
	{
		_scalingEnabled = enable;
	}

	void GuiRenderer::EndFrame()
	{
		ListDraw(&_baseDrawList);

		g_pEnv->_graphicsDevice->SetDepthBufferState(DepthBufferState::DepthDefault);
		g_pEnv->_graphicsDevice->SetCullingMode(CullingMode::BackFace);
		g_pEnv->_graphicsDevice->SetBlendState(BlendState::Opaque);
	}

	DrawList* GuiRenderer::GetDrawList()
	{
		return _drawLists.size() > 0 ?_drawLists.front() : &_baseDrawList;// _drawListOverride ? _drawListOverride : &_baseDrawList;
	}

	DrawList* GuiRenderer::PushDrawList()
	{
		auto dl = new DrawList;
		_drawLists.push_front(dl);
		return dl;
	}

	void GuiRenderer::PopDrawList()
	{
		if (_drawLists.size() > 0)
		{
			delete _drawLists.front();
			_drawLists.pop_front();
		}
	}

	void GuiRenderer::ListDraw(DrawList* list)
	{
		if (!list)
			return;

		auto gfxDevice = g_pEnv->_graphicsDevice;

		gfxDevice->SetVertexShader(_instancedShader->GetShaderStage(ShaderStage::VertexShader));
		gfxDevice->SetPixelShader(_instancedShader->GetShaderStage(ShaderStage::PixelShader));

		list->_atlas.Pack();
		/*if (auto tex = _atlas.GetAtlasTexture(); tex != nullptr)
		{
			tex->SaveToFile("GuiAtlas.png");
		}*/

		/*if (list->_scissorRects.size() > 0)
		{
			g_pEnv->_graphicsDevice->SetScissorRects(list->_scissorRects);
		}*/

		ITexture2D* lastTexture = nullptr;
		UIInstance* lastInstance = nullptr;
		int32_t offset = -1;

		for (auto i = 0ul; i < list->_instances.size(); ++i)
		{
			auto instance = list->_instances[i];

			GuiInstanceData& data = instance;

			data.instance->ResolveUvFromAtlas(&list->_atlas);

			//auto font = fontInstance.second;
			//auto size = fontInstance.first;

			auto instanceBuffer = data.instance;
			uint32_t numInstances = instanceBuffer->GetSize();

			gfxDevice->SetTexture2D(list->_atlas.GetAtlasTexture());
			gfxDevice->SetInputLayout(_instancedInputLayout);
			gfxDevice->SetTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			gfxDevice->SetIndexBuffer(_instancedIndexBuffer);
			gfxDevice->SetVertexBuffer(0, _instancedVertexBuffer);

			instanceBuffer->Finish();

			gfxDevice->DrawIndexedInstanced(
				6,
				numInstances);
		}
		//list->_instances.clear();
		list->Clear();

		g_pEnv->_graphicsDevice->ClearScissorRect();

		//_drawListOverride = nullptr;
	}

	/*void GuiRenderer::SetDrawList(DrawList* drawList)
	{
		_drawListOverride = drawList;
	}*/

	void GuiRenderer::FillQuad(int32_t x, int32_t y, int32_t width, int32_t height, const math::Color& colour)
	{
		GuiVertex vertices[4];

		if (_scalingEnabled)
		{
			x = RX(x, _screenWidth);
			y = RY(y, _screenHeight);
			width = RX(width, _screenWidth);
			height = RY(height, _screenHeight);
		}

		float x0 = (float)x;
		float y0 = (float)y;
		float x1 = x0 + (float)width;
		float y1 = y0 + (float)height;
		

		// top left
		vertices[0].position = PointToNdc(x0, y0);
		vertices[0].texcoord = math::Vector2(0.0f, 0.0f);
		vertices[0].colour = colour;

		// top right
		vertices[1].position = PointToNdc(x1, y0);
		vertices[1].texcoord = math::Vector2(1.0f, 0.0f);
		vertices[1].colour = colour;

		// bottom left
		vertices[2].position = PointToNdc(x0, y1);
		vertices[2].texcoord = math::Vector2(0.0f, 1.0f);
		vertices[2].colour = colour;

		// bottom right
		vertices[3].position = PointToNdc(x1, y1);
		vertices[3].texcoord = math::Vector2(1.0f, 1.0f);
		vertices[3].colour = colour;

		_vertexBuffer->SetVertexData((uint8_t*)vertices, sizeof(vertices));

		MeshIndexFormat indices[6] = {
			0, 1, 2,
			2, 1, 3
		};

		_indexBuffer->SetIndexData((uint8_t*)indices, sizeof(indices));

		// Per object buffer
		//
		/*auto perObjectBuffer = g_pEnv->_graphicsDevice->GetEngineConstantBuffer(EngineConstantBuffer::PerObjectBuffer);

		if (perObjectBuffer)
		{
			_perObjectBuffer._material.diffuseColour = colour;
			perObjectBuffer->Write(&_perObjectBuffer, sizeof(_perObjectBuffer));
		}*/

		g_pEnv->_graphicsDevice->SetVertexShader(_basicShader->GetShaderStage(ShaderStage::VertexShader));
		g_pEnv->_graphicsDevice->SetPixelShader(_basicShader->GetShaderStage(ShaderStage::PixelShader));
		g_pEnv->_graphicsDevice->SetInputLayout(_inputLayout);

		// We use basic white texture
		//
		g_pEnv->_graphicsDevice->SetVertexBuffer(0, _vertexBuffer);
		g_pEnv->_graphicsDevice->SetIndexBuffer(_indexBuffer);
		g_pEnv->_graphicsDevice->SetTexture2D(0, _basicWhiteTex);
		g_pEnv->_graphicsDevice->SetTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		g_pEnv->_graphicsDevice->DrawIndexed(6);
	}

	void GuiRenderer::PushFillQuad(int32_t x, int32_t y, int32_t width, int32_t height, const math::Color& colour)
	{
		PushFillQuad(GetDrawList(), x, y, width, height, colour);
	}

	void GuiRenderer::PushFillQuad(DrawList* list, int32_t x, int32_t y, int32_t width, int32_t height, const math::Color& colour)
	{
		if (_scalingEnabled)
		{
			x = RX(x, _screenWidth);
			y = RY(y, _screenHeight);
			width = RX(width, _screenWidth);
			height = RY(height, _screenHeight);
		}

		float x0 = (float)x;
		float y0 = (float)y;
		float x1 = x0 + (float)width;
		float y1 = y0 + (float)height;

		list->_atlas.AddTexture(_basicWhiteTex);

		auto instanceBuffer = list->_quadInstance;

		if (instanceBuffer)
		{
			if (instanceBuffer->HasStarted() == false)
			{
				instanceBuffer->Start();

				GuiInstanceData instance;
				instance.instance = instanceBuffer;
				//instance.texture = _basicWhiteTex;

				list->_instances.push_back(instance);
			}
		}
		else
		{
			LOG_CRIT("Trying to render instanced quad without an instance");
			return;
		}

		float scaleX = (float)width / (float)_screenWidth;
		float scaleY = (float)height / (float)_screenHeight;

		auto centrePoint = PointToNdc(
			(float)x + ((float)width / 2.0f),
			(float)y + ((float)height / 2.0f));

		instanceBuffer->Render(
			_basicWhiteTex,
			math::Vector2(centrePoint.x, centrePoint.y),
			math::Vector2(scaleX, scaleY),
			math::Vector2(0.0f, 0.0f),
			math::Vector2(1.0f, 1.0f),
			colour,
			math::Matrix::Identity);
	}

	void GuiRenderer::FillQuadVerticalGradient(int32_t x, int32_t y, int32_t width, int32_t height, const math::Color& colour1, const math::Color& colour2)
	{
		GuiVertex vertices[4];

		if (_scalingEnabled)
		{
			x = RX(x, _screenWidth);
			y = RY(y, _screenHeight);
			width = RX(width, _screenWidth);
			height = RY(height, _screenHeight);
		}

		float x0 = (float)x;// +0.5f;
		float y0 = (float)y;// +0.5f;
		float x1 = x0 + (float)width;// +0.5f;
		float y1 = y0 + (float)height;// +0.5f;

		float xx0 = 2.0f * (x0 /*- 0.5f*/) / _screenWidth - 1.0f;
		float yy0 = 1.0f - 2.0f * (y0 /*- 0.5f*/) / _screenHeight;
		float xx1 = 2.0f * (x1 /*- 0.5f*/) / _screenWidth - 1.0f;
		float yy1 = 1.0f - 2.0f * (y1 /*- 0.5f*/) / _screenHeight;



		// top left

		vertices[0].position.x = xx0;
		vertices[0].position.y = yy0;
		vertices[0].position.z = 0;
		vertices[0].texcoord = math::Vector2(0.0f, 0.0f);
		vertices[0].colour = colour1;

		// top right

		vertices[1].position.x = xx1;
		vertices[1].position.y = yy0;
		vertices[1].position.z = 0;
		vertices[1].texcoord = math::Vector2(1.0f, 0.0f);
		vertices[1].colour = colour1;

		// bottom left

		vertices[2].position.x = xx0;
		vertices[2].position.y = yy1;
		vertices[2].position.z = 0;
		vertices[2].texcoord = math::Vector2(0.0f, 1.0f);
		vertices[2].colour = colour2;

		// bottom right

		vertices[3].position.x = xx1;
		vertices[3].position.y = yy1; 
		vertices[3].position.z = 0;
		vertices[3].texcoord = math::Vector2(1.0f, 1.0f);
		vertices[3].colour = colour2;

		_vertexBuffer->SetVertexData((uint8_t*)vertices, sizeof(vertices));

		MeshIndexFormat indices[6] = {
			0, 1, 2,
			2, 1, 3
		};

		_indexBuffer->SetIndexData((uint8_t*)indices, sizeof(indices));

		// Per object buffer
		//
		/*auto perObjectBuffer = g_pEnv->_graphicsDevice->GetEngineConstantBuffer(EngineConstantBuffer::PerObjectBuffer);

		if (perObjectBuffer)
		{
			_perObjectBuffer._material.diffuseColour = math::Color(1,1,1,1);
			perObjectBuffer->Write(&_perObjectBuffer, sizeof(_perObjectBuffer));
		}*/

		// We use basic white texture
		//
		g_pEnv->_graphicsDevice->SetVertexShader(_basicShader->GetShaderStage(ShaderStage::VertexShader));
		g_pEnv->_graphicsDevice->SetPixelShader(_basicShader->GetShaderStage(ShaderStage::PixelShader));

		g_pEnv->_graphicsDevice->SetInputLayout(_inputLayout);

		g_pEnv->_graphicsDevice->SetVertexBuffer(0, _vertexBuffer);
		g_pEnv->_graphicsDevice->SetIndexBuffer(_indexBuffer);
		g_pEnv->_graphicsDevice->SetTexture2D(0, _basicWhiteTex);
		g_pEnv->_graphicsDevice->SetTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		g_pEnv->_graphicsDevice->DrawIndexed(6);
	}

	void GuiRenderer::PushFillQuadVerticalGradient(int32_t x, int32_t y, int32_t width, int32_t height, const math::Color& colour1, const math::Color& colour2)
	{
		PushFillQuadVerticalGradient(GetDrawList(), x, y, width, height, colour1, colour2);
	}

	void GuiRenderer::PushFillQuadVerticalGradient(DrawList* list, int32_t x, int32_t y, int32_t width, int32_t height, const math::Color& colour1, const math::Color& colour2)
	{
		if (_scalingEnabled)
		{
			x = RX(x, _screenWidth);
			y = RY(y, _screenHeight);
			width = RX(width, _screenWidth);
			height = RY(height, _screenHeight);
		}

		list->_atlas.AddTexture(_basicWhiteTex);

		auto instanceBuffer = list->_quadInstance;

		if (instanceBuffer)
		{
			if (instanceBuffer->HasStarted() == false)
			{
				instanceBuffer->Start();

				GuiInstanceData instance;
				instance.instance = instanceBuffer;
				//instance.texture = _basicWhiteTex;

				list->_instances.push_back(instance);
			}
		}
		else
		{
			LOG_CRIT("Trying to render instanced quad without an instance");
			return;
		}

		float scaleX = (float)width / (float)_screenWidth;
		float scaleY = (float)height / (float)_screenHeight;

		auto centrePoint = PointToNdc(
			(float)x + ((float)width / 2.0f),
			(float)y + ((float)height / 2.0f));

		instanceBuffer->Render(
			_basicWhiteTex,
			math::Vector2(centrePoint.x, centrePoint.y),
			math::Vector2(scaleX, scaleY),
			math::Vector2(0.0f, 0.0f),
			math::Vector2(1.0f, 1.0f),
			colour1,
			colour2,
			math::Matrix::Identity);
	}

	void GuiRenderer::Line(int32_t x1, int32_t y1, int32_t x2, int32_t y2, const math::Color& colour)
	{
		GuiVertex vertices[2];

		if (_scalingEnabled)
		{
			x1 = RX(x1, _screenWidth);
			y1 = RY(y1, _screenHeight);
			x2 = RX(x2, _screenWidth);
			y2 = RY(y2, _screenHeight);
		}

		vertices[0].position = PointToNdc(x1, y1);
		vertices[0].texcoord = math::Vector2(0.0f, 0.0f);
		vertices[0].colour = colour;

		vertices[1].position = PointToNdc(x2, y2);
		vertices[1].texcoord = math::Vector2(1.0f, 1.0f);
		vertices[1].colour = colour;

		_vertexBuffer->SetVertexData((uint8_t*)vertices, sizeof(vertices));

		// Per object buffer
		//
		/*auto perObjectBuffer = g_pEnv->_graphicsDevice->GetEngineConstantBuffer(EngineConstantBuffer::PerObjectBuffer);

		if (perObjectBuffer)
		{
			_perObjectBuffer._material.diffuseColour = colour;
			perObjectBuffer->Write(&_perObjectBuffer, sizeof(_perObjectBuffer));
		}*/

		MeshIndexFormat indices[2] = {
			0, 1
		};

		_indexBuffer->SetIndexData((uint8_t*)indices, sizeof(indices));

		g_pEnv->_graphicsDevice->SetVertexShader(_basicShader->GetShaderStage(ShaderStage::VertexShader));
		g_pEnv->_graphicsDevice->SetPixelShader(_basicShader->GetShaderStage(ShaderStage::PixelShader));

		g_pEnv->_graphicsDevice->SetInputLayout(_inputLayout);

		// We use basic white texture
		//
		g_pEnv->_graphicsDevice->SetIndexBuffer(_indexBuffer);
		g_pEnv->_graphicsDevice->SetVertexBuffer(0, _vertexBuffer);
		g_pEnv->_graphicsDevice->SetTexture2D(0, _basicWhiteTex);
		g_pEnv->_graphicsDevice->SetTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
		g_pEnv->_graphicsDevice->DrawIndexed(2);
	}

	void GuiRenderer::PushLine(int32_t x1, int32_t y1, int32_t x2, int32_t y2, const math::Color& colour)
	{
		GuiVertex vertices[2];

		if (_scalingEnabled)
		{
			x1 = RX(x1, _screenWidth);
			y1 = RY(y1, _screenHeight);
			x2 = RX(x2, _screenWidth);
			y2 = RY(y2, _screenHeight);
		}

		vertices[0].position = PointToNdc(x1, y1);
		vertices[0].texcoord = math::Vector2(0.0f, 0.0f);
		vertices[0].colour = colour;

		vertices[1].position = PointToNdc(x2, y2);
		vertices[1].texcoord = math::Vector2(1.0f, 1.0f);
		vertices[1].colour = colour;

		_vertexBuffer->SetVertexData((uint8_t*)vertices, sizeof(vertices));

		// Per object buffer
		//
		/*auto perObjectBuffer = g_pEnv->_graphicsDevice->GetEngineConstantBuffer(EngineConstantBuffer::PerObjectBuffer);

		if (perObjectBuffer)
		{
			_perObjectBuffer._material.diffuseColour = colour;
			perObjectBuffer->Write(&_perObjectBuffer, sizeof(_perObjectBuffer));
		}*/

		MeshIndexFormat indices[2] = {
			0, 1
		};

		_indexBuffer->SetIndexData((uint8_t*)indices, sizeof(indices));

		// We use basic white texture
		//
		g_pEnv->_graphicsDevice->SetIndexBuffer(_indexBuffer);
		g_pEnv->_graphicsDevice->SetVertexBuffer(0, _vertexBuffer);
		g_pEnv->_graphicsDevice->SetTexture2D(0, _basicWhiteTex);
		g_pEnv->_graphicsDevice->SetTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
		g_pEnv->_graphicsDevice->DrawIndexed(2);
	}

	void GuiRenderer::Frame(int32_t x, int32_t y, int32_t width, int32_t height, int32_t thickness, const math::Color& colour)
	{
		if (thickness > 1)
		{
			FillQuad(x, y, width, thickness, colour); // top
			FillQuad(x + width, y, thickness, height + thickness, colour); // right
			FillQuad(x, y, thickness, height, colour); // left
			FillQuad(x, y + height, width, thickness, colour);
		}
		else
		{
			Line(x, y, x + width, y, colour); // top
			Line(x + width, y, x + width, y + height, colour); // right
			Line(x, y, x, y + height, colour); // left
			Line(x, y + height, x + width, y + height, colour); // bottom
		}
	}

	void GuiRenderer::PushFrame(int32_t x, int32_t y, int32_t width, int32_t height, int32_t thickness, const math::Color& colour)
	{
		//if (thickness > 1)
		{
			PushFillQuad(x, y, width, thickness, colour); // top
			PushFillQuad(x + width, y, thickness, height + thickness, colour); // right
			PushFillQuad(x, y, thickness, height, colour); // left
			PushFillQuad(x, y + height, width, thickness, colour);
		}
		//else
		//{
		//	Line(x, y, x + width, y, colour); // top
		//	Line(x + width, y, x + width, y + height, colour); // right
		//	Line(x, y, x, y + height, colour); // left
		//	Line(x, y + height, x + width, y + height, colour); // bottom
		//}
	}

	void GuiRenderer::FillTexturedQuad(ITexture2D* texture, int32_t x, int32_t y, int32_t width, int32_t height, const math::Color& colour)
	{
		if (_scalingEnabled)
		{
			x = RX(x, _screenWidth);
			y = RY(y, _screenHeight);
			width = RX(width, _screenWidth);
			height = RY(height, _screenHeight);
		}

		GuiVertex vertices[4];

		float x0 = (float)x;
		float y0 = (float)y;
		float x1 = x0 + (float)width;
		float y1 = y0 + (float)height;

		float xx0 = 2.0f * (x0 /*- 0.5f*/) / _screenWidth - 1.0f;
		float yy0 = 1.0f - 2.0f * (y0 /*- 0.5f*/) / _screenHeight;
		float xx1 = 2.0f * (x1 /*- 0.5f*/) / _screenWidth - 1.0f;
		float yy1 = 1.0f - 2.0f * (y1 /*- 0.5f*/) / _screenHeight;

		// top left

		vertices[0].position.x = xx0;
		vertices[0].position.y = yy0;
		vertices[0].position.z = 0;
		vertices[0].texcoord = math::Vector2(0.0f, 0.0f);
		vertices[0].colour = colour;

		// top right

		vertices[1].position.x = xx1;
		vertices[1].position.y = yy0;
		vertices[1].position.z = 0;
		vertices[1].texcoord = math::Vector2(1.0f, 0.0f);
		vertices[1].colour = colour;

		// bottom left

		vertices[2].position.x = xx0;
		vertices[2].position.y = yy1;
		vertices[2].position.z = 0;
		vertices[2].texcoord = math::Vector2(0.0f, 1.0f);
		vertices[2].colour = colour;

		// bottom right

		vertices[3].position.x = xx1;
		vertices[3].position.y = yy1;
		vertices[3].position.z = 0;
		vertices[3].texcoord = math::Vector2(1.0f, 1.0f);
		vertices[3].colour = colour;

		_vertexBuffer->SetVertexData((uint8_t*)vertices, sizeof(vertices));

		MeshIndexFormat indices[6] = {
			0, 1, 2,
			2, 1, 3
		};

		_indexBuffer->SetIndexData((uint8_t*)indices, sizeof(indices));

		// Per object buffer
		//
		/*auto perObjectBuffer = g_pEnv->_graphicsDevice->GetEngineConstantBuffer(EngineConstantBuffer::PerObjectBuffer);

		if (perObjectBuffer)
		{
			_perObjectBuffer._material.diffuseColour = colour;
			perObjectBuffer->Write(&_perObjectBuffer, sizeof(_perObjectBuffer));
		}*/

		g_pEnv->_graphicsDevice->SetVertexShader(_basicShader->GetShaderStage(ShaderStage::VertexShader));
		g_pEnv->_graphicsDevice->SetPixelShader(_basicShader->GetShaderStage(ShaderStage::PixelShader));

		g_pEnv->_graphicsDevice->SetInputLayout(_inputLayout);

		// We use basic white texture
		//
		g_pEnv->_graphicsDevice->SetTexture2D(texture);
		g_pEnv->_graphicsDevice->SetTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		g_pEnv->_graphicsDevice->SetVertexBuffer(0, _vertexBuffer);
		g_pEnv->_graphicsDevice->SetIndexBuffer(_indexBuffer);
		g_pEnv->_graphicsDevice->DrawIndexed(6);
	}

	void GuiRenderer::FillTexturedQuad(ITexture2D* texture, int32_t x, int32_t y, int32_t width, int32_t height, math::Vector2 uv[2], const math::Color& colour)
	{
		if (_scalingEnabled)
		{
			x = RX(x, _screenWidth);
			y = RY(y, _screenHeight);
			width = RX(width, _screenWidth);
			height = RY(height, _screenHeight);
		}

		GuiVertex vertices[4];

		float x0 = (float)x;
		float y0 = (float)y;
		float x1 = x0 + (float)width;
		float y1 = y0 + (float)height;

		float xx0 = 2.0f * (x0 /*- 0.5f*/) / _screenWidth - 1.0f;
		float yy0 = 1.0f - 2.0f * (y0 /*- 0.5f*/) / _screenHeight;
		float xx1 = 2.0f * (x1 /*- 0.5f*/) / _screenWidth - 1.0f;
		float yy1 = 1.0f - 2.0f * (y1 /*- 0.5f*/) / _screenHeight;

		// top left

		vertices[0].position.x = xx0;
		vertices[0].position.y = yy0;
		vertices[0].position.z = 0;
		vertices[0].texcoord = uv[0];
		vertices[0].colour = colour;

		// top right

		vertices[1].position.x = xx1;
		vertices[1].position.y = yy0;
		vertices[1].position.z = 0;
		vertices[1].texcoord = math::Vector2(uv[1].x, uv[0].y);
		vertices[1].colour = colour;

		// bottom left

		vertices[2].position.x = xx0;
		vertices[2].position.y = yy1;
		vertices[2].position.z = 0;
		vertices[2].texcoord = math::Vector2(uv[0].x, uv[1].y);
		vertices[2].colour = colour;

		// bottom right

		vertices[3].position.x = xx1;
		vertices[3].position.y = yy1;
		vertices[3].position.z = 0;
		vertices[3].texcoord = uv[1];
		vertices[3].colour = colour;

		_vertexBuffer->SetVertexData((uint8_t*)vertices, sizeof(vertices));

		MeshIndexFormat indices[6] = {
			0, 1, 2,
			2, 1, 3
		};

		_indexBuffer->SetIndexData((uint8_t*)indices, sizeof(indices));

		// Per object buffer
		//
		/*auto perObjectBuffer = g_pEnv->_graphicsDevice->GetEngineConstantBuffer(EngineConstantBuffer::PerObjectBuffer);

		if (perObjectBuffer)
		{
			_perObjectBuffer._material.diffuseColour = colour;
			perObjectBuffer->Write(&_perObjectBuffer, sizeof(_perObjectBuffer));
		}*/

		g_pEnv->_graphicsDevice->SetVertexShader(_basicShader->GetShaderStage(ShaderStage::VertexShader));
		g_pEnv->_graphicsDevice->SetPixelShader(_basicShader->GetShaderStage(ShaderStage::PixelShader));

		g_pEnv->_graphicsDevice->SetInputLayout(_inputLayout);

		// We use basic white texture
		//
		g_pEnv->_graphicsDevice->SetTexture2D(texture);
		g_pEnv->_graphicsDevice->SetTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		g_pEnv->_graphicsDevice->SetVertexBuffer(0, _vertexBuffer);
		g_pEnv->_graphicsDevice->SetIndexBuffer(_indexBuffer);
		g_pEnv->_graphicsDevice->DrawIndexed(6);
	}

	void GuiRenderer::PushFillTexturedQuad(ITexture2D* texture, int32_t x, int32_t y, int32_t width, int32_t height, const math::Color& colour, float rotation)
	{
		PushFillTexturedQuad(GetDrawList(), texture, x, y, width, height, colour, rotation);
	}

	void GuiRenderer::PushFillTexturedQuad(DrawList* list, ITexture2D* texture, int32_t x, int32_t y, int32_t width, int32_t height, const math::Color& colour, float rotation)
	{
		if (!texture)
			return;

		if (_scalingEnabled)
		{
			x = RX(x, _screenWidth);
			y = RY(y, _screenHeight);
			width = RX(width, _screenWidth);
			height = RY(height, _screenHeight);
		}

		/*if (rotation != 0.0f)
		{
			float hw = (float)width * 0.5f;
			float hh = (float)height * 0.5f;

			float cx = (float)x + hw;
			float cy = (float)y + hh;

			x = cx + cos(ToRadian(rotation)) * hw;
			y = cy - sin(ToRadian(rotation)) * hh;
		}*/

		list->_atlas.AddTexture(texture);

		float x0 = (float)x;
		float y0 = (float)y;
		float x1 = x0 + (float)width;
		float y1 = y0 + (float)height;

		auto instanceBuffer = list->_quadInstance;

		if (instanceBuffer)
		{
			if (instanceBuffer->HasStarted() == false)
			{
				instanceBuffer->Start();

				GuiInstanceData instance;
				instance.instance = instanceBuffer;
				//instance.texture = texture;

				list->_instances.push_back(instance);
			}
		}
		else
		{
			LOG_CRIT("Trying to render instanced quad without an instance");
			return;
		}

		float scaleX = (float)width / (float)_screenWidth;
		float scaleY = (float)height / (float)_screenHeight;

		auto centrePoint = PointToNdc(
			(float)x + ((float)width / 2.0f),
			(float)y + ((float)height / 2.0f));

		auto rotMat = rotation != 0.0f ? math::Matrix::CreateRotationZ(ToRadian(rotation)) : math::Matrix::Identity;

		instanceBuffer->Render(
			texture,
			math::Vector2(centrePoint.x, centrePoint.y),
			math::Vector2(scaleX, scaleY),
			math::Vector2(0.0f, 0.0f),
			math::Vector2(1.0f, 1.0f),
			colour,
			rotMat);
	}

	void GuiRenderer::FullScreenTexturedQuad(ITexture2D* texture)
	{
		GuiVertex vertices[4];

		float x0 = 0.0f;
		float y0 = 0.0f;
		float x1 = x0 + (float)_screenWidth;
		float y1 = y0 + (float)_screenHeight;

		float xx0 = 2.0f * (x0 /*- 0.5f*/) / _screenWidth - 1.0f;
		float yy0 = 1.0f - 2.0f * (y0 /*- 0.5f*/) / _screenHeight;
		float xx1 = 2.0f * (x1 /*- 0.5f*/) / _screenWidth - 1.0f;
		float yy1 = 1.0f - 2.0f * (y1 /*- 0.5f*/) / _screenHeight;

		// top left

		vertices[0].position.x = xx0;
		vertices[0].position.y = yy0;
		vertices[0].position.z = 0;
		vertices[0].texcoord = math::Vector2(0.0f, 0.0f);
		vertices[0].colour = math::Color(1, 1, 1, 1);

		// top right

		vertices[1].position.x = xx1;
		vertices[1].position.y = yy0;
		vertices[1].position.z = 0;
		vertices[1].texcoord = math::Vector2(1.0f, 0.0f);
		vertices[1].colour = math::Color(1, 1, 1, 1);

		// bottom left

		vertices[2].position.x = xx0;
		vertices[2].position.y = yy1;
		vertices[2].position.z = 0;
		vertices[2].texcoord = math::Vector2(0.0f, 1.0f);
		vertices[2].colour = math::Color(1, 1, 1, 1);

		// bottom right

		vertices[3].position.x = xx1;
		vertices[3].position.y = yy1;
		vertices[3].position.z = 0;
		vertices[3].texcoord = math::Vector2(1.0f, 1.0f);
		vertices[3].colour = math::Color(1, 1, 1, 1);

		_vertexBuffer->SetVertexData((uint8_t*)vertices, sizeof(vertices));

		MeshIndexFormat indices[6] = {
			0, 1, 2,
			2, 1, 3
		};

		_indexBuffer->SetIndexData((uint8_t*)indices, sizeof(indices));

		// Per object buffer
		//
		/*auto perObjectBuffer = g_pEnv->_graphicsDevice->GetEngineConstantBuffer(EngineConstantBuffer::PerObjectBuffer);

		if (perObjectBuffer)
		{
			_perObjectBuffer._material.diffuseColour = math::Color(1,1,1,1);
			perObjectBuffer->Write(&_perObjectBuffer, sizeof(_perObjectBuffer));
		}*/

		// We use basic white texture
		//
		if(texture)
			g_pEnv->_graphicsDevice->SetTexture2D(texture);

		g_pEnv->_graphicsDevice->SetTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		g_pEnv->_graphicsDevice->SetVertexBuffer(0, _vertexBuffer);
		g_pEnv->_graphicsDevice->SetIndexBuffer(_indexBuffer);
		g_pEnv->_graphicsDevice->DrawIndexed(6);
	}

	void GuiRenderer::HalfScreenTexturedQuad(ITexture2D* texture)
	{
		GuiVertex vertices[4];

		float width = (float)_screenWidth * 0.5f;
		float height = (float)_screenHeight * 0.5f;

		float x0 = 0.0f;
		float y0 = 0.0f;
		float x1 = x0 + (float)width;
		float y1 = y0 + (float)height;

		float xx0 = 2.0f * (x0 /*- 0.5f*/) / width - 1.0f;
		float yy0 = 1.0f - 2.0f * (y0 /*- 0.5f*/) / height;
		float xx1 = 2.0f * (x1 /*- 0.5f*/) / width - 1.0f;
		float yy1 = 1.0f - 2.0f * (y1 /*- 0.5f*/) / height;

		// top left

		vertices[0].position.x = xx0;
		vertices[0].position.y = yy0;
		vertices[0].position.z = 0;
		vertices[0].texcoord = math::Vector2(0.0f, 0.0f);
		vertices[0].colour = math::Color(1, 1, 1, 1);

		// top right

		vertices[1].position.x = xx1;
		vertices[1].position.y = yy0;
		vertices[1].position.z = 0;
		vertices[1].texcoord = math::Vector2(1.0f, 0.0f);
		vertices[1].colour = math::Color(1, 1, 1, 1);

		// bottom left

		vertices[2].position.x = xx0;
		vertices[2].position.y = yy1;
		vertices[2].position.z = 0;
		vertices[2].texcoord = math::Vector2(0.0f, 1.0f);
		vertices[2].colour = math::Color(1, 1, 1, 1);

		// bottom right

		vertices[3].position.x = xx1;
		vertices[3].position.y = yy1;
		vertices[3].position.z = 0;
		vertices[3].texcoord = math::Vector2(1.0f, 1.0f);
		vertices[3].colour = math::Color(1, 1, 1, 1);

		_vertexBuffer->SetVertexData((uint8_t*)vertices, sizeof(vertices));

		MeshIndexFormat indices[6] = {
			0, 1, 2,
			2, 1, 3
		};

		_indexBuffer->SetIndexData((uint8_t*)indices, sizeof(indices));

		// Per object buffer
		//
		//auto perObjectBuffer = g_pEnv->_graphicsDevice->GetEngineConstantBuffer(EngineConstantBuffer::PerObjectBuffer);

		//if (perObjectBuffer)
		//{
		//	PerObjectBuffer bufferData;
		//	//bufferData._material.diffuseColour = colour;
		//	perObjectBuffer->Write(&bufferData, sizeof(bufferData));
		//}

		// We use basic white texture
		//
		if (texture)
			g_pEnv->_graphicsDevice->SetTexture2D(texture);

		g_pEnv->_graphicsDevice->SetTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		g_pEnv->_graphicsDevice->SetVertexBuffer(0, _vertexBuffer);
		g_pEnv->_graphicsDevice->SetIndexBuffer(_indexBuffer);
		g_pEnv->_graphicsDevice->DrawIndexed(6);
	}

	void GuiRenderer::DoubleScreenTexturedQuad(ITexture2D* texture)
	{
		GuiVertex vertices[4];

		float width = (float)_screenWidth * 2.0f;
		float height = (float)_screenHeight * 2.0f;

		float x0 = 0.0f;
		float y0 = 0.0f;
		float x1 = x0 + (float)width;
		float y1 = y0 + (float)height;

		float xx0 = 2.0f * (x0 /*- 0.5f*/) / width - 1.0f;
		float yy0 = 1.0f - 2.0f * (y0 /*- 0.5f*/) / height;
		float xx1 = 2.0f * (x1 /*- 0.5f*/) / width - 1.0f;
		float yy1 = 1.0f - 2.0f * (y1 /*- 0.5f*/) / height;

		// top left

		vertices[0].position.x = xx0;
		vertices[0].position.y = yy0;
		vertices[0].position.z = 0;
		vertices[0].texcoord = math::Vector2(0.0f, 0.0f);
		vertices[0].colour = math::Color(1, 1, 1, 1);

		// top right

		vertices[1].position.x = xx1;
		vertices[1].position.y = yy0;
		vertices[1].position.z = 0;
		vertices[1].texcoord = math::Vector2(1.0f, 0.0f);
		vertices[1].colour = math::Color(1, 1, 1, 1);

		// bottom left

		vertices[2].position.x = xx0;
		vertices[2].position.y = yy1;
		vertices[2].position.z = 0;
		vertices[2].texcoord = math::Vector2(0.0f, 1.0f);
		vertices[2].colour = math::Color(1, 1, 1, 1);

		// bottom right

		vertices[3].position.x = xx1;
		vertices[3].position.y = yy1;
		vertices[3].position.z = 0;
		vertices[3].texcoord = math::Vector2(1.0f, 1.0f);
		vertices[3].colour = math::Color(1, 1, 1, 1);

		_vertexBuffer->SetVertexData((uint8_t*)vertices, sizeof(vertices));

		MeshIndexFormat indices[6] = {
			0, 1, 2,
			2, 1, 3
		};

		_indexBuffer->SetIndexData((uint8_t*)indices, sizeof(indices));

		// Per object buffer
		//
		//auto perObjectBuffer = g_pEnv->_graphicsDevice->GetEngineConstantBuffer(EngineConstantBuffer::PerObjectBuffer);

		//if (perObjectBuffer)
		//{
		//	PerObjectBuffer bufferData = {};
		//	//bufferData._material.diffuseColour = colour;
		//	perObjectBuffer->Write(&bufferData, sizeof(bufferData));
		//}

		// We use basic white texture
		//
		if (texture)
			g_pEnv->_graphicsDevice->SetTexture2D(texture);

		g_pEnv->_graphicsDevice->SetTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		g_pEnv->_graphicsDevice->SetVertexBuffer(0, _vertexBuffer);
		g_pEnv->_graphicsDevice->SetIndexBuffer(_indexBuffer);
		g_pEnv->_graphicsDevice->DrawIndexed(6);
	}

	void GuiRenderer::FullScreenTexturedQuad(ITexture2D* texture, IShader* shader)
	{
		if (shader)
		{
			g_pEnv->_graphicsDevice->SetPixelShader(shader->GetShaderStage(ShaderStage::PixelShader));
			g_pEnv->_graphicsDevice->SetVertexShader(shader->GetShaderStage(ShaderStage::VertexShader));

			FullScreenTexturedQuad(texture);

			// restore the shaders
			//
			g_pEnv->_graphicsDevice->SetPixelShader(_basicShader->GetShaderStage(ShaderStage::PixelShader));
			g_pEnv->_graphicsDevice->SetVertexShader(_basicShader->GetShaderStage(ShaderStage::VertexShader));
		}
		else
		{
			FullScreenTexturedQuad(texture);
		}
	}

	void GuiRenderer::HalfScreenTexturedQuad(ITexture2D* texture, IShader* shader)
	{
		if (shader)
		{
			g_pEnv->_graphicsDevice->SetPixelShader(shader->GetShaderStage(ShaderStage::PixelShader));
			g_pEnv->_graphicsDevice->SetVertexShader(shader->GetShaderStage(ShaderStage::VertexShader));

			HalfScreenTexturedQuad(texture);

			// restore the shaders
			//
			g_pEnv->_graphicsDevice->SetPixelShader(_basicShader->GetShaderStage(ShaderStage::PixelShader));
			g_pEnv->_graphicsDevice->SetVertexShader(_basicShader->GetShaderStage(ShaderStage::VertexShader));
		}
		else
		{
			HalfScreenTexturedQuad(texture);
		}
	}

	void GuiRenderer::DoubleScreenTexturedQuad(ITexture2D* texture, IShader* shader)
	{
		if (shader)
		{
			g_pEnv->_graphicsDevice->SetPixelShader(shader->GetShaderStage(ShaderStage::PixelShader));
			g_pEnv->_graphicsDevice->SetVertexShader(shader->GetShaderStage(ShaderStage::VertexShader));

			HalfScreenTexturedQuad(texture);

			// restore the shaders
			//
			g_pEnv->_graphicsDevice->SetPixelShader(_basicShader->GetShaderStage(ShaderStage::PixelShader));
			g_pEnv->_graphicsDevice->SetVertexShader(_basicShader->GetShaderStage(ShaderStage::VertexShader));
		}
		else
		{
			HalfScreenTexturedQuad(texture);
		}
	}

	math::Vector4 GuiRenderer::PointToNdc(int x, int y)
	{
		return PointToNdc((float)x, (float)y);
	}

	math::Vector4 GuiRenderer::PointToNdc(float x, float y)
	{
		math::Vector4 p;

		float cx = _screenWidth;
		float cy = _screenHeight;

		p.x = 2.0f * x / cx - 1.0f;
		p.y = 1.0f - 2.0f * y / cy;

		p.z = 0.0f;// z;

		return p;
	}

	void GuiRenderer::PrintText(IFontResource* font, uint8_t fontSize, int32_t x, int32_t y, const math::Color& colour, uint8_t align, const std::wstring& text)
	{
		if (!font)
			return;

		if (text.length() == 0)
			return;

#if 0
		if (_scalingEnabled)
		{
			float originalAspect = (float)DEV_RESOLUTION_X;// / (float)DEV_RESOLUTION_Y;
			float newAspect = (float)_screenWidth;// / (float)_screenHeight;
			float fontSizeMultiplier = newAspect / originalAspect;

			if (fontSizeMultiplier != 1.0f)
			{

				int32_t targetFontSize = (int32_t)(float)fontSize * fontSizeMultiplier;

				if (targetFontSize != fontSize)
				{
					int32_t lastGoodFontSize = fontSize;
					int32_t dir = targetFontSize - fontSize < 0 ? -1 : 1;
					int32_t fontTempSize = fontSize;

					while (true)
					{
						fontTempSize += dir;
						if (font->HasFontSize(fontTempSize))
							lastGoodFontSize = fontTempSize;

						if (dir == -1)
						{
							if (lastGoodFontSize <= targetFontSize)
								break;
						}
						else if (dir == 1)
						{
							if (lastGoodFontSize >= targetFontSize)
								break;
						}

						if (fontTempSize < 1)
							break;
					}

					fontSize = lastGoodFontSize;
				}
			}
		}
#endif

		// Make sure the font supports this size
		//
		if (font->HasFontSize(fontSize) == false)
			return;


		uint32_t numCharsToAlloc = (uint32_t)text.length() + 1;
		auto numQuads = numCharsToAlloc;
		auto numTriangles = numQuads * 2;
		auto numVertices = numQuads * 4;
		auto numIndexes = numTriangles * 3;

		if (_fontVertexBuffer == nullptr || _fontIndexBuffer == nullptr || _cachedFontBufferSize == 0 || numCharsToAlloc > _cachedFontBufferSize)
		{
			SAFE_DELETE(_fontVertexBuffer);
			SAFE_DELETE(_fontIndexBuffer);

			LOG_DEBUG("Rebuilding font buffers for string '%S': numQuads=%d, numTriangles=%d, numVertices=%d, numIndices=%d", text.c_str(), numQuads, numTriangles, numVertices, numIndexes);

			_fontVertexBuffer = g_pEnv->_graphicsDevice->CreateVertexBuffer(
				(4 * sizeof(GuiVertex)) * (numCharsToAlloc + 1000),
				sizeof(GuiVertex),
				D3D11_USAGE_DYNAMIC,
				D3D11_CPU_ACCESS_WRITE);			

			std::vector<MeshIndexFormat> indices(numIndexes + 6000);

			for (UINT i = 0; i < (numIndexes + 6000) / 6; ++i)
			{
				indices[i * 6] = i * 4;
				indices[i * 6 + 1] = i * 4 + 1;
				indices[i * 6 + 2] = i * 4 + 2;
				indices[i * 6 + 3] = i * 4;
				indices[i * 6 + 4] = i * 4 + 2;
				indices[i * 6 + 5] = i * 4 + 3;
			}

			_fontIndexBuffer = g_pEnv->_graphicsDevice->CreateIndexBuffer(
				((numIndexes + 6000) * sizeof(MeshIndexFormat)),
				sizeof(MeshIndexFormat),
				D3D11_USAGE_DEFAULT,
				0,
				indices.data());

			_cachedFontBufferSize = numCharsToAlloc + 1000;
		}

		if (align != 0)
		{
			int32_t textw, texth;
			font->MeasureText(fontSize, text, textw, texth);

			if ((align & FontAlign::CentreLR) != 0)
				x -= textw / 2;

			if ((align & FontAlign::CentreUD) != 0)
				y -= texth / 2;

			if ((align & FontAlign::Right) != 0)
				x -= textw;
		}

		// Allocate the list of vertices (4 for each character)
		GuiVertex* vertices = new GuiVertex[numVertices];

		int32_t i = 0;
		int32_t sx = x;
		int32_t sy = y;

		if (_scalingEnabled)
		{
			sx = RX(sx, _screenWidth);
			sy = RY(sy, _screenHeight);
		}

		wchar_t lastCh = 0;
		bool didBaseline = false;

		int32_t ox = sx;

		for(auto it = text.begin(); it != text.end(); it++)
		{
			auto ch = *it;

			if (ch == '\n')
			{
				sx = ox;
				sy += fontSize;
				continue;
			}

			auto glyphDesc = font->GetGlyphFromChar(fontSize, ch);

			if (!glyphDesc)
			{
				delete[] vertices;
				return;
			}

			int32_t width = glyphDesc->width;
			int32_t height = glyphDesc->totalHeight;	

			sx += glyphDesc->offsetX;

			vertices[i + 0].position = PointToNdc(sx, sy + height);
			vertices[i + 1].position = PointToNdc(sx, sy);
			vertices[i + 2].position = PointToNdc(sx + width, sy);
			vertices[i + 3].position = PointToNdc(sx + width, sy + height);

			vertices[i + 0].texcoord.x = glyphDesc->uv0[0];
			vertices[i + 0].texcoord.y = glyphDesc->uv1[1];
			vertices[i + 0].colour = colour;

			vertices[i + 1].texcoord.x = glyphDesc->uv0[0];
			vertices[i + 1].texcoord.y = glyphDesc->uv0[1];
			vertices[i + 1].colour = colour;

			vertices[i + 2].texcoord.x = glyphDesc->uv1[0];
			vertices[i + 2].texcoord.y = glyphDesc->uv0[1];
			vertices[i + 2].colour = colour;

			vertices[i + 3].texcoord.x = glyphDesc->uv1[0];
			vertices[i + 3].texcoord.y = glyphDesc->uv1[1];
			vertices[i + 3].colour = colour;

			sx += (glyphDesc->advanceX >> 6);

			lastCh = ch;

			sx -= glyphDesc->offsetX;

			i += 4;
		}
			
		_fontVertexBuffer->SetVertexData((uint8_t*)vertices, sizeof(GuiVertex)* numVertices);

		g_pEnv->_graphicsDevice->SetTexture2D(font->GetAtlas(fontSize));

		g_pEnv->_graphicsDevice->SetVertexShader(_basicShader->GetShaderStage(ShaderStage::VertexShader));
		g_pEnv->_graphicsDevice->SetPixelShader(_basicShader->GetShaderStage(ShaderStage::PixelShader));

		g_pEnv->_graphicsDevice->SetInputLayout(_inputLayout);
		g_pEnv->_graphicsDevice->SetTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		g_pEnv->_graphicsDevice->SetIndexBuffer(_fontIndexBuffer);
		g_pEnv->_graphicsDevice->SetVertexBuffer(0, _fontVertexBuffer);
		g_pEnv->_graphicsDevice->SetVertexBuffer(1, nullptr);

		g_pEnv->_graphicsDevice->DrawIndexed(numIndexes);

		SAFE_DELETE_ARRAY(vertices);
	}

	void GuiRenderer::PushPrintText(IFontResource* font, uint8_t fontSize, int32_t x, int32_t y, const math::Color& colour, uint8_t align, const std::wstring& text)
	{
		PushPrintText(GetDrawList(), font, fontSize, x, y, colour, align, text);
	}

	void GuiRenderer::PushScissorRect(const RECT& rect)
	{
		PushScissorRect(GetDrawList(), rect);
	}

	void GuiRenderer::PushScissorRect(DrawList* list, const RECT& rect)
	{
		if (list)
		{
			//list->_scissorRects.push_back(rect);
		}
	}

	void GuiRenderer::PushPrintText(DrawList* list, IFontResource* font, uint8_t fontSize, int32_t x, int32_t y, const math::Color& colour, uint8_t align, const std::wstring& text)
	{
		if (!font)
			return;

		if (text.length() == 0)
			return;

		// Make sure the font supports this size
		//
		if (font->HasFontSize(fontSize) == false)
			return;

		list->_atlas.AddTexture(font->GetAtlas(fontSize));

		if (align != 0)
		{
			int32_t textw, texth;
			font->MeasureText(fontSize, text, textw, texth);

			if ((align & FontAlign::CentreLR) != 0)
				x -= textw / 2;

			if ((align & FontAlign::CentreUD) != 0)
				y -= texth / 2;

			if ((align & FontAlign::Right) != 0)
				x -= textw;
		}

		auto instanceBuffer = list->_quadInstance;//font->GetInstanceBuffer(fontSize);

		if (instanceBuffer)
		{
			if (instanceBuffer->HasStarted() == false)
			{
				instanceBuffer->Start();

				GuiInstanceData instance;
				instance.instance = instanceBuffer;
				//instance.texture = font->GetAtlas(fontSize);

				list->_instances.push_back(instance);
			}
		}
		else
		{
			LOG_CRIT("Trying to render instanced text using a font not owning an instance");
			return;
		}

		int32_t i = 0;
		int32_t sx = x;
		int32_t sy = y;

		if (_scalingEnabled)
		{
			sx = RX(sx, _screenWidth);
			sy = RY(sy, _screenHeight);
		}

		wchar_t lastCh = 0;
		bool didBaseline = false;

		for (auto it = text.begin(); it != text.end(); it++)
		{
			auto ch = *it;

			auto glyphDesc = font->GetGlyphFromChar(fontSize, ch);

			if (!glyphDesc)
				return;

			int32_t width = glyphDesc->width;
			int32_t height = glyphDesc->totalHeight;

			sx += glyphDesc->offsetX;

			float scaleX = (float)width / (float)_screenWidth;
			float scaleY = (float)height / (float)_screenHeight;

			auto centrePoint = PointToNdc(
				(float)sx + ((float)width / 2.0f),
				(float)sy + ((float)height / 2.0f));

			instanceBuffer->Render(
				font->GetAtlas(fontSize),
				math::Vector2(centrePoint.x, centrePoint.y),
				math::Vector2(scaleX, scaleY),
				math::Vector2(glyphDesc->uv0),
				math::Vector2(glyphDesc->uv1),
				colour,
				math::Matrix::Identity);

			sx += (glyphDesc->advanceX >> 6);

			lastCh = ch;

			sx -= glyphDesc->offsetX;

			i += 4;
		}
	}
}