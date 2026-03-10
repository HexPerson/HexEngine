
#pragma once

#include <HexEngine.Core/HexEngine.hpp>
#include <Ultralight/Ultralight.h>
#include <Ultralight/../AppCore/Window.h>
#include <Ultralight/../AppCore/App.h>
#include <Ultralight/../AppCore/Overlay.h>
#include <Ultralight/../AppCore/Platform.h>
#include <Ultralight/../AppCore/Monitor.h>

using namespace ultralight;

namespace HexCreator
{
	class Surface : public ultralight::Surface
	{
	public:
        Surface(uint32_t width, uint32_t height)
        {
            Resize(width, height);
        }

        virtual ~Surface()
        {
            SAFE_DELETE(_staging);
            SAFE_DELETE(_texture);
        }

        virtual uint32_t width() const override { return _staging->GetWidth(); }

        virtual uint32_t height() const override { return _staging->GetHeight(); }

        virtual uint32_t row_bytes() const override
        {
            return _staging->GetWidth() * 4;
        }

        virtual size_t size() const override { return row_bytes() * height(); }

        virtual void* LockPixels() override
        {
            int32_t rowPitch;
            auto mapped = _staging->LockPixels(&rowPitch);

            // ignore this
            /*if (mapped)
            {
                if (rowPitch != row_bytes())
                {
                    UnlockPixels();
                    Resize(rowPitch / 4, height());
                    return nullptr;
                }

            }*/

            return mapped;
        }

        virtual void UnlockPixels() override
        {
            _staging->UnlockPixels();
        }

        virtual void Resize(uint32_t width, uint32_t height) override
        {
            SAFE_DELETE(_texture);
            SAFE_DELETE(_staging);

            _texture = HexEngine::g_pEnv->_graphicsDevice->CreateTexture2D(
                width, height,
                DXGI_FORMAT_B8G8R8A8_UNORM,
                1,
                D3D11_BIND_SHADER_RESOURCE,
                0,
                1,
                0,
                nullptr,
                (D3D11_CPU_ACCESS_FLAG)0,
                D3D11_RTV_DIMENSION_UNKNOWN,
                D3D11_UAV_DIMENSION_UNKNOWN,
                D3D11_SRV_DIMENSION_TEXTURE2D,
                D3D11_DSV_DIMENSION_UNKNOWN,
                D3D11_USAGE_DEFAULT);

            _staging = HexEngine::g_pEnv->_graphicsDevice->CreateTexture2D(
                width, height,
                DXGI_FORMAT_B8G8R8A8_UNORM,
                1,
                0/*D3D11_BIND_SHADER_RESOURCE*/,
                0,
                1,
                0,
                nullptr,
                D3D11_CPU_ACCESS_WRITE,
                D3D11_RTV_DIMENSION_UNKNOWN,
                D3D11_UAV_DIMENSION_UNKNOWN,
                D3D11_SRV_DIMENSION_UNKNOWN,
                D3D11_DSV_DIMENSION_UNKNOWN,
                D3D11_USAGE_STAGING);
        }

        void Update()
        {
            if (dirty_bounds_.IsEmpty() == false)
            {
                RECT rect = {
                    dirty_bounds_.x(),
                    dirty_bounds_.y(),
                    dirty_bounds_.x() + dirty_bounds_.width(),
                    dirty_bounds_.y() + dirty_bounds_.height()
                };

                _staging->CopyTo(_texture,
                    rect,
                    rect);

                this->ClearDirtyBounds();
            }
        }

	//private:
        HexEngine::ITexture2D* _texture = nullptr;
        HexEngine::ITexture2D* _staging = nullptr;
	};

    class Tile
    {
    public:
        Tile(RefPtr<Renderer> renderer, int width, int height, double scale)
        {
            ViewConfig view_config;
            view_config.initial_device_scale = 1;//scale;
            view_config.is_accelerated = false;
            _view = renderer->CreateView(width, height, view_config, nullptr);
        }

        Tile(RefPtr<View> existing_view) : _view(existing_view)
        {
        }

        RefPtr<View> view() { return _view; }

        Surface* surface() { return static_cast<Surface*>(_view->surface()); }

    private:
        RefPtr<View> _view;
    };

	class SurfaceFactory : public ultralight::SurfaceFactory
	{
	public:
		virtual ultralight::Surface* CreateSurface(uint32_t width, uint32_t height) override;

		virtual void DestroySurface(ultralight::Surface* surface) override;
	};

	class Window : public HexEngine::Window
	{
	public:
        Window(int32_t width, int32_t height);

        void set_listener(WindowListener* listener) { _listener = listener; }
        WindowListener* listener() { return _listener; }

    private:
        WindowListener* _listener = nullptr;
	};
}
