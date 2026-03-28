

#pragma once

#include "../Required.hpp"
#include "../Graphics/IVertexBuffer.hpp"
#include "../Graphics/IIndexBuffer.hpp"
#include "../Graphics/IInputLayout.hpp"
#include "../Graphics/ITexture2D.hpp"
#include "../Graphics/IFontResource.hpp"
#include "../Environment/IEnvironment.hpp"
#include "../Graphics/IGraphicsDevice.hpp"
#include "Elements/Style.hpp"

#include "DrawList.hpp"

namespace HexEngine
{
	class IShader;

	struct GuiVertex
	{
		math::Vector4 position;
		math::Vector2 texcoord;
		math::Color colour;
	};

	struct InstancedGuiVertex
	{
		math::Vector4 position;
	};

	

	class HEX_API GuiRenderer
	{
	public:
		GuiRenderer();
		~GuiRenderer();

		void StartFrame(uint32_t width = 0, uint32_t height = 0);
		void EndFrame();

		void FillQuad(int32_t x, int32_t y, int32_t width, int32_t height, const math::Color& colour);
		void FillQuadVerticalGradient(int32_t x, int32_t y, int32_t width, int32_t height, const math::Color& colour1, const math::Color& colour2);
		void Frame(int32_t x, int32_t y, int32_t width, int32_t height, int32_t thickness, const math::Color& colour);
		void FillTexturedQuad(ITexture2D* texture, int32_t x, int32_t y, int32_t width, int32_t height, const math::Color& colour, float rotation = 0.0f);
		void FillTexturedQuad(ITexture2D* texture, int32_t x, int32_t y, int32_t width, int32_t height, math::Vector2 uv[2], const math::Color& colour, float rotation = 0.0f);
		void FillTexturedQuadWithShader(ITexture2D* texture, int32_t x, int32_t y, int32_t width, int32_t height, const math::Color& colour, IShader* shader, float rotation = 0.0f);
		void FillTexturedQuadWithShader(ITexture2D* texture, int32_t x, int32_t y, int32_t width, int32_t height, math::Vector2 uv[2], const math::Color& colour, IShader* shader, float rotation = 0.0f);

		void PrintText(IFontResource* font, uint8_t fontSize, int32_t x, int32_t y, const math::Color& colour, uint8_t align, const std::wstring& text);
		void PrintText(IFontResource* font, uint8_t fontSize, int32_t x, int32_t y, const math::Color& colour, uint8_t align, const std::wstring& text, const TextEffectSettings& effects);
		void Line(int32_t x1, int32_t y1, int32_t x2, int32_t y2, const math::Color& colour);

		void FullScreenTexturedQuad(ITexture2D* texture);
		void HalfScreenTexturedQuad(ITexture2D* texture);
		void DoubleScreenTexturedQuad(ITexture2D* texture);
		void FullScreenTexturedQuad(ITexture2D* texture, IShader* shader);
		void HalfScreenTexturedQuad(ITexture2D* texture, IShader* shader);
		void DoubleScreenTexturedQuad(ITexture2D* texture, IShader* shader);

		// Instanced drawing routines
		void PushFillQuad(int32_t x, int32_t y, int32_t width, int32_t height, const math::Color& colour);
		void PushFillQuadVerticalGradient(int32_t x, int32_t y, int32_t width, int32_t height, const math::Color& colour1, const math::Color& colour2);
		void PushFrame(int32_t x, int32_t y, int32_t width, int32_t height, int32_t thickness, const math::Color& colour);
		void PushFillTexturedQuad(ITexture2D* texture, int32_t x, int32_t y, int32_t width, int32_t height, const math::Color& colour, float rotation = 0.0f);
		void PushPrintText(IFontResource* font, uint8_t fontSize, int32_t x, int32_t y, const math::Color& colour, uint8_t align, const std::wstring& text);
		void PushPrintText(IFontResource* font, uint8_t fontSize, int32_t x, int32_t y, const math::Color& colour, uint8_t align, const std::wstring& text, const TextEffectSettings& effects);
		void PushLine(int32_t x1, int32_t y1, int32_t x2, int32_t y2, const math::Color& colour);

		void PushFillQuad(DrawList* list, int32_t x, int32_t y, int32_t width, int32_t height, const math::Color& colour);
		void PushFillQuadVerticalGradient(DrawList* list, int32_t x, int32_t y, int32_t width, int32_t height, const math::Color& colour1, const math::Color& colour2);
		void PushFrame(DrawList* list, int32_t x, int32_t y, int32_t width, int32_t height, int32_t thickness, const math::Color& colour);
		void PushFillTexturedQuad(DrawList* list, ITexture2D* texture, int32_t x, int32_t y, int32_t width, int32_t height, const math::Color& colour, float rotation = 0.0f);
		void PushPrintText(DrawList* list, IFontResource* font, uint8_t fontSize, int32_t x, int32_t y, const math::Color& colour, uint8_t align, const std::wstring& text);
		void PushPrintText(DrawList* list, IFontResource* font, uint8_t fontSize, int32_t x, int32_t y, const math::Color& colour, uint8_t align, const std::wstring& text, const TextEffectSettings& effects);
		void PushLine(DrawList* list, int32_t x1, int32_t y1, int32_t x2, int32_t y2, const math::Color& colour);

		void PushFullScreenTexturedQuad(ITexture2D* texture);
		void PushHalfScreenTexturedQuad(ITexture2D* texture);
		void PushDoubleScreenTexturedQuad(ITexture2D* texture);
		void PushFullScreenTexturedQuad(ITexture2D* texture, IShader* shader);
		void PushHalfScreenTexturedQuad(ITexture2D* texture, IShader* shader);
		void PushDoubleScreenTexturedQuad(ITexture2D* texture, IShader* shader);

		void PushScissorRect(const RECT& rect);
		void PushScissorRect(DrawList* list, const RECT& rect);

		void EnableScaling(bool enable);

		void ListDraw(DrawList* drawList);
		//void SetDrawList(DrawList* drawList);
		DrawList* GetDrawList();

		DrawList* PushDrawList();
		void PopDrawList();

	private:
		math::Vector4 PointToNdc(int x, int y);
		math::Vector4 PointToNdc(float x, float y);
		bool CreateInstancedBuffers();

	private:
		Camera* _currentCamera = nullptr;
		std::shared_ptr<IShader> _basicShader;
		std::shared_ptr<IShader> _basicHdrShader;
		std::shared_ptr<IShader> _instancedShader;
		std::shared_ptr<IShader> _instancedHdrShader;
		IShader* _activeBasicShader = nullptr;
		IShader* _activeInstancedShader = nullptr;

		IVertexBuffer* _vertexBuffer = nullptr;
		IIndexBuffer* _indexBuffer = nullptr;

		IVertexBuffer* _fontVertexBuffer = nullptr;
		IIndexBuffer* _fontIndexBuffer = nullptr;

		IVertexBuffer* _instancedVertexBuffer = nullptr;
		IIndexBuffer* _instancedIndexBuffer = nullptr;
		uint32_t _cachedFontBufferSize = 0;

		IInputLayout* _inputLayout = nullptr;
		IInputLayout* _instancedInputLayout = nullptr;
		std::shared_ptr<ITexture2D> _basicWhiteTex;

		uint32_t _screenWidth = 0;
		uint32_t _screenHeight = 0;

		bool _scalingEnabled = false;

		PerObjectBuffer _perObjectBuffer;

		//std::vector<GuiInstanceData> _instancesToRender;
		//UIInstance* _quadInstance = nullptr;

		DrawList _baseDrawList;
		std::list<DrawList*> _drawLists;
		//DrawList* _drawListOverride = nullptr;

	public:
		Style _style;
		
	};

#define RX(x,w) (int32_t)(((float)x / (float)HexEngine::g_pEnv->GetScreenWidth()) * (float)w)
#define RY(y,h) (int32_t)(((float)y / (float)HexEngine::g_pEnv->GetScreenHeight()) * (float)h)
#define SCALEX(x) RY(x, HexEngine::g_pEnv->GetScreenWidth())
#define SCALEY(y) RY(y, HexEngine::g_pEnv->GetScreenHeight())
}
