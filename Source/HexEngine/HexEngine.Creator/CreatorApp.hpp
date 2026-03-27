
#pragma once

#include <HexEngine.Core/HexEngine.hpp>
#include <Ultralight/Ultralight.h>
#include <Ultralight/../AppCore/Window.h>
#include <Ultralight/../AppCore/App.h>
#include <Ultralight/../AppCore/Overlay.h>
#include <Ultralight/../AppCore/Platform.h>
#include <Ultralight/../AppCore/Monitor.h>
#include "Window.hpp"

using namespace ultralight;

namespace HexCreator
{
	class CreatorApp :
		public WindowListener,
		public ViewListener,
        public LoadListener,
        public HexEngine::IInputListener
	{
	public:
        CreatorApp() {
            ///
            /// Create our main App instance.
            ///
            /// The App class is responsible for the lifetime of the application and is required to create
            /// any windows.
            ///
            Settings settings;
            settings.force_cpu_renderer = true;
            Config config;
            config.scroll_timer_delay = 1.0 / 90.0;
            //config.force_repaint = true;
            
            auto& platform = ultralight::Platform::instance();

            platform.set_config(config);
            platform.set_font_loader(GetPlatformFontLoader());
            platform.set_file_system(GetPlatformFileSystem("./assets"));
            platform.set_logger(GetDefaultLogger("ultralight.log"));
            platform.set_surface_factory(new SurfaceFactory);

            _renderer = Renderer::Create();

            //_app = App::Create(settings, config);

            ///
            /// Create our Window.
            ///
            /// This command creates a native platform window and shows it immediately.
            /// 
            /// The window's size (900 by 600) is in virtual device coordinates, the actual size in pixels
            /// is automatically determined by the monitor's DPI.
            ///
            /// 
            auto width = 3840;//_app->main_monitor()->width();
            auto height = 2160;//_app->main_monitor()->height();
            double scale = 1.25;//_app->main_monitor()->scale();
            
            _window = new Window(
                (uint32_t)((float)width / scale),
                (uint32_t)((float)height / scale)/*,
                false,
                kWindowFlags_Titled | kWindowFlags_Maximizable | kWindowFlags_Resizable*/);

            Window* window2 = new Window(800, 600);
            window2->set_listener(this);
            //CreateTile(800, 600, 1.0f);

            ///
            /// Set the title of our window.
            ///
           // _window->SetTitle("Ultralight Sample 2 - Basic App");
         

            ///
            /// Create a web-content overlay that spans the entire window.
            ///
            /// You can create multiple overlays per window, each overlay has its own View which can be
            /// used to load and display web-content.
            ///
            /// AppCore automatically manages focus, keyboard/mouse input, and GPU painting for each active
            /// overlay. Destroying the overlay will remove it from the window.
            ///
           // _mainView = Overlay::Create(_window, _window->width(), _window->height(), 200, 0);
           // _sidebar = Overlay::Create(_window, 200, 100, 0, 0);

           // OnResize(_window.get(), _window->width(), _window->height());
            //_window->MoveTo(0, 0);

            

            ///
            /// Load a local HTML file into our overlay's View
            ///
            //_mainView->view()->LoadURL("file:///content.html");
            //_sidebar->view()->LoadURL("file:///sidebar.html");
            //overlay_->view()->LoadURL("https://google.co.uk");

            ///
            /// Register our MyApp instance as a WindowListener so we can handle the Window's OnClose event
            /// below.
            ///
            _window->set_listener(this);

            ///
            /// Register our MyApp instance as a ViewListener so we can handle the View's OnChangeCursor
            /// event below.
            ///
            //_mainView->view()->set_view_listener(this);
            //_sidebar->view()->set_view_listener(this);
        }

        Tile* CreateTile(int32_t width, int32_t height, double scale)
        {
            Tile* tile = new Tile(_renderer, width, height, scale);

            tile->view()->LoadURL("file:///sidebar.html");
            tile->view()->set_view_listener(this);
            tile->view()->set_load_listener(this);
            tile->view()->Focus();

            _tiles.push_back(tile);

            return tile;
        }

        /*Tile* CreateDialogWin(int32_t width, int32_t height, double scale)
        {
            Window* window = new Window(width, height);

            Tile* tile = new Tile(_renderer, width, height, scale);

            tile->view()->LoadURL("file:///sidebar.html");
            tile->view()->set_view_listener(this);
            tile->view()->set_load_listener(this);
            tile->view()->Focus();

            _tiles.push_back(tile);

            return tile;
        }*/

        virtual ~CreatorApp() {}

        virtual bool OnInputEvent(HexEngine::InputEvent event, HexEngine::InputData* data) override
        {
            switch (event)
            {
            case HexEngine::InputEvent::MouseMove:
            {
                ultralight::MouseEvent evt;
                evt.type = ultralight::MouseEvent::kType_MouseMoved;
                evt.x = (int32_t)data->MouseMove.x;
                evt.y = (int32_t)data->MouseMove.y;
                evt.button = ultralight::MouseEvent::Button::kButton_None;
                OnMouseEvent(evt);
                break;
            }

            case HexEngine::InputEvent::MouseDown:
            {
                ultralight::MouseEvent evt;
                evt.type = ultralight::MouseEvent::kType_MouseDown;
                evt.x = data->MouseDown.xpos;
                evt.y = data->MouseDown.ypos;

                if(data->MouseDown.button == VK_LBUTTON)
                    evt.button = ultralight::MouseEvent::Button::kButton_Left;
                else if (data->MouseDown.button == VK_RBUTTON)
                    evt.button = ultralight::MouseEvent::Button::kButton_Right;
                else if (data->MouseDown.button == VK_MBUTTON)
                    evt.button = ultralight::MouseEvent::Button::kButton_Middle;

                OnMouseEvent(evt);
                break;
            }
            }

            return true;
        }

        ///
        /// Inherited from WindowListener, called when the Window is closed.
        /// 
        /// We exit the application when the window is closed.
        ///
        virtual void OnClose(ultralight::Window* window) override {
            _app->Quit();
        }

        ///
        /// Inherited from WindowListener, called when the Window is resized.
        /// 
        /// (Not used in this sample)
        ///
        virtual void OnResize(ultralight::Window* window, uint32_t width, uint32_t height) override
        {
            //uint32_t left_pane_width_px = _window->ScreenToPixels((float)width * 0.2f);
            //_sidebar->Resize(left_pane_width_px, height);

            //// Calculate the width of our right pane (window width - left width)
            //int right_pane_width = (int)width - left_pane_width_px;

            //// Clamp our right pane's width to a minimum of 1
            //right_pane_width = right_pane_width > 1 ? right_pane_width : 1;

            //_mainView->Resize((uint32_t)right_pane_width, height);

            //_sidebar->MoveTo(0, 0);
            //_mainView->MoveTo(left_pane_width_px, 0);
        }

        ///
        /// Inherited from ViewListener, called when the Cursor changes.
        ///
        virtual void OnChangeCursor(ultralight::View* caller, ultralight::Cursor cursor) override {
            //_window->SetCursor(cursor);
        }

        /*virtual void OnAddConsoleMessage(ultralight::View* caller, MessageSource source,
            MessageLevel level, const String& message, uint32_t line_number,
            uint32_t column_number, const String& source_id) override
        {
            
            switch (level)
            {
            case MessageLevel::kMessageLevel_Error:
                MessageBoxA(0, message.utf8().data(), 0, 0);
                break;
            }
        }*/

        virtual bool OnMouseEvent(const MouseEvent& evt) override 
        {
            for (auto& tile : _tiles)
            {
                //tile->view()->Focus();
                //tile->view()->Focus();
                tile->view()->FireMouseEvent(evt);
            }

            return false;
        }

        /*RefPtr<ultralight::Window> window()
        {
            return _window;
        }*/

        /*RefPtr<Overlay> mainView()
        {
            return _mainView;
        }*/

        void Run() {
            _renderer->Update();
            _renderer->RefreshDisplay(0);
            _renderer->Render();
            //_app->Run();
        }

    public:
        Window* _window = nullptr;
        std::vector<Tile*> _tiles;

	private:
		RefPtr<App> _app;
		
		//RefPtr<Overlay> _mainView;
       // RefPtr<Overlay> _sidebar;
        RefPtr<Renderer> _renderer;
        
	};
}