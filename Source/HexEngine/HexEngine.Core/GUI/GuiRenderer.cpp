

#include "GuiRenderer.hpp"
#include "../HexEngine.hpp"

namespace
{
	struct TextRenderPass
	{
		int32_t offsetX = 0;
		int32_t offsetY = 0;
		math::Color colour = math::Color(1.0f, 1.0f, 1.0f, 1.0f);
	};

	bool HasTextEffect(HexEngine::TextEffectFlags value, HexEngine::TextEffectFlags flag)
	{
		return (static_cast<uint8_t>(value) & static_cast<uint8_t>(flag)) != 0;
	}

	math::Color ScaleTextEffectAlpha(const math::Color& colour, float alphaScale)
	{
		return math::Color(colour.x, colour.y, colour.z, colour.w * alphaScale);
	}

	std::vector<TextRenderPass> BuildTextPasses(const HexEngine::TextEffectSettings& effects)
	{
		std::vector<TextRenderPass> passes;

		if (HasTextEffect(effects.flags, HexEngine::TextEffectFlags::Shadow))
		{
			passes.push_back({ effects.shadowOffsetX, effects.shadowOffsetY, effects.shadowColour });
		}

		if (HasTextEffect(effects.flags, HexEngine::TextEffectFlags::Glow) && effects.glowRadius > 0)
		{
			for (int32_t step = effects.glowRadius; step >= 1; --step)
			{
				float alphaScale = static_cast<float>(step) / static_cast<float>(effects.glowRadius + 1);
				auto glowColour = ScaleTextEffectAlpha(effects.glowColour, alphaScale);
				passes.push_back({ -step, 0, glowColour });
				passes.push_back({ step, 0, glowColour });
				passes.push_back({ 0, -step, glowColour });
				passes.push_back({ 0, step, glowColour });
				passes.push_back({ -step, -step, glowColour });
				passes.push_back({ step, -step, glowColour });
				passes.push_back({ -step, step, glowColour });
				passes.push_back({ step, step, glowColour });
			}
		}

		if (HasTextEffect(effects.flags, HexEngine::TextEffectFlags::Outline) && effects.outlineThickness > 0)
		{
			for (int32_t step = 1; step <= effects.outlineThickness; ++step)
			{
				passes.push_back({ -step, 0, effects.outlineColour });
				passes.push_back({ step, 0, effects.outlineColour });
				passes.push_back({ 0, -step, effects.outlineColour });
				passes.push_back({ 0, step, effects.outlineColour });
				passes.push_back({ -step, -step, effects.outlineColour });
				passes.push_back({ step, -step, effects.outlineColour });
				passes.push_back({ -step, step, effects.outlineColour });
				passes.push_back({ step, step, effects.outlineColour });
			}
		}

		return passes;
	}
}

namespace HexEngine
{
	GuiRenderer::GuiRenderer()
	{
		_basicShader = IShader::Create("EngineData.Shaders/UIBasic.hcs");
		_basicHdrShader = IShader::Create("EngineData.Shaders/UIBasicHDR.hcs");
		_instancedShader = IShader::Create("EngineData.Shaders/UIInstanced.hcs");
		_instancedHdrShader = IShader::Create("EngineData.Shaders/UIInstancedHDR.hcs");

		_activeBasicShader = _basicShader.get();
		_activeInstancedShader = _instancedShader.get();

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

		const bool hdrBackBuffer = gfxDevice->GetBackBuffer() != nullptr && gfxDevice->GetBackBuffer()->GetFormat() == DXGI_FORMAT_R16G16B16A16_FLOAT;
		_activeBasicShader = hdrBackBuffer && _basicHdrShader ? _basicHdrShader.get() : _basicShader.get();
		_activeInstancedShader = hdrBackBuffer && _instancedHdrShader ? _instancedHdrShader.get() : _instancedShader.get();

		gfxDevice->SetVertexShader(_activeBasicShader->GetShaderStage(ShaderStage::VertexShader));
		gfxDevice->SetPixelShader(_activeBasicShader->GetShaderStage(ShaderStage::PixelShader));

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

		_currentCamera = g_pEnv->_sceneManager->GetCurrentScene()->GetMainCamera();
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

		if (list->_instances.size() == 0)
			return;

		auto gfxDevice = g_pEnv->_graphicsDevice;

		gfxDevice->SetVertexShader(_activeInstancedShader->GetShaderStage(ShaderStage::VertexShader));
		gfxDevice->SetPixelShader(_activeInstancedShader->GetShaderStage(ShaderStage::PixelShader));

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

		g_pEnv->_graphicsDevice->SetVertexShader(_activeBasicShader->GetShaderStage(ShaderStage::VertexShader));
		g_pEnv->_graphicsDevice->SetPixelShader(_activeBasicShader->GetShaderStage(ShaderStage::PixelShader));
		g_pEnv->_graphicsDevice->SetInputLayout(_inputLayout);

		// We use basic white texture
		//
		g_pEnv->_graphicsDevice->SetVertexBuffer(0, _vertexBuffer);
		g_pEnv->_graphicsDevice->SetIndexBuffer(_indexBuffer);
		g_pEnv->_graphicsDevice->SetTexture2D(0, _basicWhiteTex.get());
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

		list->_atlas.AddTexture(_basicWhiteTex.get());

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
			_basicWhiteTex.get(),
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
		g_pEnv->_graphicsDevice->SetVertexShader(_activeBasicShader->GetShaderStage(ShaderStage::VertexShader));
		g_pEnv->_graphicsDevice->SetPixelShader(_activeBasicShader->GetShaderStage(ShaderStage::PixelShader));

		g_pEnv->_graphicsDevice->SetInputLayout(_inputLayout);

		g_pEnv->_graphicsDevice->SetVertexBuffer(0, _vertexBuffer);
		g_pEnv->_graphicsDevice->SetIndexBuffer(_indexBuffer);
		g_pEnv->_graphicsDevice->SetTexture2D(0, _basicWhiteTex.get());
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

		list->_atlas.AddTexture(_basicWhiteTex.get());

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
			_basicWhiteTex.get(),
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

		g_pEnv->_graphicsDevice->SetVertexShader(_activeBasicShader->GetShaderStage(ShaderStage::VertexShader));
		g_pEnv->_graphicsDevice->SetPixelShader(_activeBasicShader->GetShaderStage(ShaderStage::PixelShader));

		g_pEnv->_graphicsDevice->SetInputLayout(_inputLayout);

		// We use basic white texture
		//
		g_pEnv->_graphicsDevice->SetIndexBuffer(_indexBuffer);
		g_pEnv->_graphicsDevice->SetVertexBuffer(0, _vertexBuffer);
		g_pEnv->_graphicsDevice->SetTexture2D(0, _basicWhiteTex.get());
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
		g_pEnv->_graphicsDevice->SetTexture2D(0, _basicWhiteTex.get());
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

	void GuiRenderer::FillTexturedQuad(ITexture2D* texture, int32_t x, int32_t y, int32_t width, int32_t height, const math::Color& colour, float rotation)
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

		g_pEnv->_graphicsDevice->SetVertexShader(_activeBasicShader->GetShaderStage(ShaderStage::VertexShader));
		g_pEnv->_graphicsDevice->SetPixelShader(_activeBasicShader->GetShaderStage(ShaderStage::PixelShader));

		g_pEnv->_graphicsDevice->SetInputLayout(_inputLayout);

		// We use basic white texture
		//
		g_pEnv->_graphicsDevice->SetTexture2D(texture);
		g_pEnv->_graphicsDevice->SetTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		g_pEnv->_graphicsDevice->SetVertexBuffer(0, _vertexBuffer);
		g_pEnv->_graphicsDevice->SetIndexBuffer(_indexBuffer);
		g_pEnv->_graphicsDevice->DrawIndexed(6);
	}

	void GuiRenderer::FillTexturedQuadWithShader(ITexture2D* texture, int32_t x, int32_t y, int32_t width, int32_t height, const math::Color& colour, IShader* shader, float rotation)
	{
		if (!shader)
		{
			FillTexturedQuad(texture, x, y, width, height, colour, rotation);
			return;
		}

		auto previousShader = _activeBasicShader;
		_activeBasicShader = shader;
		FillTexturedQuad(texture, x, y, width, height, colour, rotation);
		_activeBasicShader = previousShader;
	}

	void GuiRenderer::FillTexturedQuad(ITexture2D* texture, int32_t x, int32_t y, int32_t width, int32_t height, math::Vector2 uv[2], const math::Color& colour, float rotation)
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

		g_pEnv->_graphicsDevice->SetVertexShader(_activeBasicShader->GetShaderStage(ShaderStage::VertexShader));
		g_pEnv->_graphicsDevice->SetPixelShader(_activeBasicShader->GetShaderStage(ShaderStage::PixelShader));

		g_pEnv->_graphicsDevice->SetInputLayout(_inputLayout);

		// We use basic white texture
		//
		g_pEnv->_graphicsDevice->SetTexture2D(texture);
		g_pEnv->_graphicsDevice->SetTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		g_pEnv->_graphicsDevice->SetVertexBuffer(0, _vertexBuffer);
		g_pEnv->_graphicsDevice->SetIndexBuffer(_indexBuffer);
		g_pEnv->_graphicsDevice->DrawIndexed(6);
	}

	void GuiRenderer::FillTexturedQuadWithShader(ITexture2D* texture, int32_t x, int32_t y, int32_t width, int32_t height, math::Vector2 uv[2], const math::Color& colour, IShader* shader, float rotation)
	{
		if (!shader)
		{
			FillTexturedQuad(texture, x, y, width, height, uv, colour, rotation);
			return;
		}

		auto previousShader = _activeBasicShader;
		_activeBasicShader = shader;
		FillTexturedQuad(texture, x, y, width, height, uv, colour, rotation);
		_activeBasicShader = previousShader;
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
			g_pEnv->_graphicsDevice->SetPixelShader(_activeBasicShader->GetShaderStage(ShaderStage::PixelShader));
			g_pEnv->_graphicsDevice->SetVertexShader(_activeBasicShader->GetShaderStage(ShaderStage::VertexShader));
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
			g_pEnv->_graphicsDevice->SetPixelShader(_activeBasicShader->GetShaderStage(ShaderStage::PixelShader));
			g_pEnv->_graphicsDevice->SetVertexShader(_activeBasicShader->GetShaderStage(ShaderStage::VertexShader));
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
			g_pEnv->_graphicsDevice->SetPixelShader(_activeBasicShader->GetShaderStage(ShaderStage::PixelShader));
			g_pEnv->_graphicsDevice->SetVertexShader(_activeBasicShader->GetShaderStage(ShaderStage::VertexShader));
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

		float cx = (float)_screenWidth;
		float cy = (float)_screenHeight;

		p.x = 2.0f * x / cx - 1.0f;
		p.y = 1.0f - 2.0f * y / cy;

		p.z = 0.0f;// z;

		return p;
	}

	void GuiRenderer::PrintText(IFontResource* font, uint8_t fontSize, int32_t x, int32_t y, const math::Color& colour, uint8_t align, const std::wstring& text)
	{
		PrintText(font, fontSize, x, y, colour, align, text, TextEffectSettings());
	}

	void GuiRenderer::PrintText(IFontResource* font, uint8_t fontSize, int32_t x, int32_t y, const math::Color& colour, uint8_t align, const std::wstring& text, const TextEffectSettings& effects)
	{
		if (!font || text.empty())
			return;

		if (font->HasFontSize(fontSize) == false)
			return;

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

		auto effectPasses = BuildTextPasses(effects);
		uint32_t glyphCount = 0;
		for (auto ch : text)
		{
			if (ch != '\n')
				++glyphCount;
		}

		if (glyphCount == 0)
			return;

		const uint32_t quadsPerGlyph = 1 + static_cast<uint32_t>(effectPasses.size());
		const uint32_t numCharsToAlloc = glyphCount * quadsPerGlyph + 1;
		auto numQuads = numCharsToAlloc;
		auto numTriangles = numQuads * 2;
		auto numIndexes = numTriangles * 3;

		if (_fontVertexBuffer == nullptr || _fontIndexBuffer == nullptr || _cachedFontBufferSize == 0 || numCharsToAlloc > _cachedFontBufferSize)
		{
			SAFE_DELETE(_fontVertexBuffer);
			SAFE_DELETE(_fontIndexBuffer);

			_fontVertexBuffer = g_pEnv->_graphicsDevice->CreateVertexBuffer(
				(4 * sizeof(GuiVertex)) * (numCharsToAlloc + 1000),
				sizeof(GuiVertex),
				D3D11_USAGE_DYNAMIC,
				D3D11_CPU_ACCESS_WRITE);

			std::vector<MeshIndexFormat> indices(numIndexes + 6000);
			for (UINT index = 0; index < (numIndexes + 6000) / 6; ++index)
			{
				indices[index * 6] = index * 4;
				indices[index * 6 + 1] = index * 4 + 1;
				indices[index * 6 + 2] = index * 4 + 2;
				indices[index * 6 + 3] = index * 4;
				indices[index * 6 + 4] = index * 4 + 2;
				indices[index * 6 + 5] = index * 4 + 3;
			}

			_fontIndexBuffer = g_pEnv->_graphicsDevice->CreateIndexBuffer(
				((numIndexes + 6000) * sizeof(MeshIndexFormat)),
				sizeof(MeshIndexFormat),
				D3D11_USAGE_DEFAULT,
				0,
				indices.data());

			_cachedFontBufferSize = numCharsToAlloc + 1000;
		}

		auto atlas = font->GetAtlas(fontSize);
		std::vector<GuiVertex> vertices(glyphCount * quadsPerGlyph * 4);

		int32_t sx = x;
		int32_t sy = y;
		if (_scalingEnabled)
		{
			sx = RX(sx, _screenWidth);
			sy = RY(sy, _screenHeight);
		}

		const int32_t lineStartX = sx;
		int32_t lineHeight = fontSize;
		wchar_t lastCh = 0;
		uint32_t vertexIndex = 0;

		auto emitQuad = [&](int32_t drawX, int32_t drawY, int32_t width, int32_t height, GlyphDesc* glyphDesc, const math::Color& passColour)
		{
			vertices[vertexIndex + 0].position = PointToNdc(drawX, drawY + height);
			vertices[vertexIndex + 1].position = PointToNdc(drawX, drawY);
			vertices[vertexIndex + 2].position = PointToNdc(drawX + width, drawY);
			vertices[vertexIndex + 3].position = PointToNdc(drawX + width, drawY + height);

			vertices[vertexIndex + 0].texcoord = math::Vector2(glyphDesc->uv0[0], glyphDesc->uv1[1]);
			vertices[vertexIndex + 1].texcoord = math::Vector2(glyphDesc->uv0[0], glyphDesc->uv0[1]);
			vertices[vertexIndex + 2].texcoord = math::Vector2(glyphDesc->uv1[0], glyphDesc->uv0[1]);
			vertices[vertexIndex + 3].texcoord = math::Vector2(glyphDesc->uv1[0], glyphDesc->uv1[1]);

			vertices[vertexIndex + 0].colour = passColour;
			vertices[vertexIndex + 1].colour = passColour;
			vertices[vertexIndex + 2].colour = passColour;
			vertices[vertexIndex + 3].colour = passColour;
			vertexIndex += 4;
		};

		for (auto ch : text)
		{
			if (ch == '\n')
			{
				sx = lineStartX;
				sy += lineHeight;
				lastCh = 0;
				continue;
			}

			auto glyphDesc = font->GetGlyphFromChar(fontSize, ch);
			if (!glyphDesc)
				continue;

			lineHeight = glyphDesc->totalHeight > 0 ? glyphDesc->totalHeight : lineHeight;

			int32_t penX = sx;
			int32_t drawX = penX + glyphDesc->offsetX;
			int32_t drawY = sy;

			for (const auto& pass : effectPasses)
				emitQuad(drawX + pass.offsetX, drawY + pass.offsetY, glyphDesc->width, glyphDesc->totalHeight, glyphDesc, pass.colour);

			emitQuad(drawX, drawY, glyphDesc->width, glyphDesc->totalHeight, glyphDesc, colour);

			sx = penX + (glyphDesc->advanceX >> 6);
			lastCh = ch;
		}

		const uint32_t usedVertexCount = vertexIndex;
		const uint32_t usedIndexCount = (usedVertexCount / 4) * 6;
		if (usedVertexCount == 0)
			return;

		_fontVertexBuffer->SetVertexData(reinterpret_cast<uint8_t*>(vertices.data()), sizeof(GuiVertex) * usedVertexCount);

		g_pEnv->_graphicsDevice->SetTexture2D(atlas);
		g_pEnv->_graphicsDevice->SetVertexShader(_activeBasicShader->GetShaderStage(ShaderStage::VertexShader));
		g_pEnv->_graphicsDevice->SetPixelShader(_activeBasicShader->GetShaderStage(ShaderStage::PixelShader));
		g_pEnv->_graphicsDevice->SetInputLayout(_inputLayout);
		g_pEnv->_graphicsDevice->SetTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		g_pEnv->_graphicsDevice->SetIndexBuffer(_fontIndexBuffer);
		g_pEnv->_graphicsDevice->SetVertexBuffer(0, _fontVertexBuffer);
		g_pEnv->_graphicsDevice->SetVertexBuffer(1, nullptr);
		g_pEnv->_graphicsDevice->DrawIndexed(usedIndexCount);
	}

	void GuiRenderer::PushPrintText(IFontResource* font, uint8_t fontSize, int32_t x, int32_t y, const math::Color& colour, uint8_t align, const std::wstring& text)
	{
		PushPrintText(GetDrawList(), font, fontSize, x, y, colour, align, text, TextEffectSettings());
	}

	void GuiRenderer::PushPrintText(IFontResource* font, uint8_t fontSize, int32_t x, int32_t y, const math::Color& colour, uint8_t align, const std::wstring& text, const TextEffectSettings& effects)
	{
		PushPrintText(GetDrawList(), font, fontSize, x, y, colour, align, text, effects);
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
		PushPrintText(list, font, fontSize, x, y, colour, align, text, TextEffectSettings());
	}

	void GuiRenderer::PushPrintText(DrawList* list, IFontResource* font, uint8_t fontSize, int32_t x, int32_t y, const math::Color& colour, uint8_t align, const std::wstring& text, const TextEffectSettings& effects)
	{
		if (!list || !font || text.empty())
			return;

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

		auto instanceBuffer = list->_quadInstance;
		if (!instanceBuffer)
		{
			LOG_CRIT("Trying to render instanced text using a font not owning an instance");
			return;
		}

		if (instanceBuffer->HasStarted() == false)
		{
			instanceBuffer->Start();

			GuiInstanceData instance;
			instance.instance = instanceBuffer;
			list->_instances.push_back(instance);
		}

		auto atlas = font->GetAtlas(fontSize);
		auto effectPasses = BuildTextPasses(effects);

		int32_t sx = x;
		int32_t sy = y;
		if (_scalingEnabled)
		{
			sx = RX(sx, _screenWidth);
			sy = RY(sy, _screenHeight);
		}

		const int32_t lineStartX = sx;
		int32_t lineHeight = fontSize;
		wchar_t lastCh = 0;

		auto emitInstance = [&](int32_t drawX, int32_t drawY, int32_t drawHeight, GlyphDesc* glyphDesc, const math::Color& passColour)
		{
			float scaleX = static_cast<float>(glyphDesc->width) / static_cast<float>(_screenWidth);
			float scaleY = static_cast<float>(drawHeight) / static_cast<float>(_screenHeight);
			auto centrePoint = PointToNdc(
				static_cast<float>(drawX) + (static_cast<float>(glyphDesc->width) / 2.0f),
				static_cast<float>(drawY) + (static_cast<float>(drawHeight) / 2.0f));

			instanceBuffer->Render(
				atlas,
				math::Vector2(centrePoint.x, centrePoint.y),
				math::Vector2(scaleX, scaleY),
				math::Vector2(glyphDesc->uv0),
				math::Vector2(glyphDesc->uv1),
				passColour,
				math::Matrix::Identity);
		};

		for (auto ch : text)
		{
			if (ch == '\n')
			{
				sx = lineStartX;
				sy += lineHeight;
				lastCh = 0;
				continue;
			}

			auto glyphDesc = font->GetGlyphFromChar(fontSize, ch);
			if (!glyphDesc)
				continue;

			lineHeight = glyphDesc->totalHeight > 0 ? glyphDesc->totalHeight : lineHeight;

			int32_t penX = sx;
			int32_t drawX = penX + glyphDesc->offsetX;
			int32_t drawY = sy;

			for (const auto& pass : effectPasses)
				emitInstance(drawX + pass.offsetX, drawY + pass.offsetY, glyphDesc->totalHeight, glyphDesc, pass.colour);

			emitInstance(drawX, drawY, glyphDesc->totalHeight, glyphDesc, colour);

			sx = penX + (glyphDesc->advanceX >> 6);
			lastCh = ch;
		}
	}
}
