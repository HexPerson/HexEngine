
#include "Game3DEnvironment.hpp"
#include "../FileSystem/FileSystem.hpp"
#include "TimeManager.hpp"
#include "LogFile.hpp"
#include "../FileSystem/KeyValues.hpp"
#include "../FileSystem/ResourceSystem.hpp"
#include "../FileSystem/ICompressionProvider.hpp"
#include "../FileSystem/AssetPackageManager.hpp"
#include "../Graphics/ShaderSystem.hpp"
#include "../Graphics/MaterialLoader.hpp"
#include "../Scene/SceneManager.hpp"
#include "../Input/InputSystem.hpp"
#include "../Graphics/DebugRenderer.hpp"
#include "../Graphics/ISSAOProvider.hpp"
#include "../Input/Hvar.hpp"
#include "../Terrain/ChunkManager.hpp"
#include "../Plugin/PluginSystem.hpp"
#include "../GUI/UIManager.hpp"
#include "../Graphics/IconService.hpp"
#include "../Scripting/IScriptEngine.hpp"
#include "../Graphics/IDenoiserProvider.hpp"
#include "../Graphics/MeshLoader.hpp"

#define USE_MULTITHREADED_PHYSICS 0


namespace HexEngine
{
	HVar cl_simulationRate("cl_simulationRate", "The speed at which physics is simulated in hz", 120.0f, 1.0f, 240.0f);
	HVar cl_showfps("cl_showfps", "Draw the current frames per-second on the screen", false, false, true);
	HVar cl_forcelagframe("cl_forcelagframe", "Forcefully delay each frame by this amount (in milliseconds)", 0, 0, 1000);

	Game3DEnvironment::Game3DEnvironment() :
		_running(false)
	{
	}

	/// <summary>
	/// Create a 3D game environment
	/// </summary>
	/// <param name="options">The engine options</param>
	/// <returns>A pointer to a Game3DEnvironment instance</returns>
	Game3DEnvironment* Game3DEnvironment::Create(const Game3DOptions& options)
	{
		//CoInitialize(nullptr);

		CoInitializeEx(NULL, COINIT_MULTITHREADED | COINIT_APARTMENTTHREADED);

		// Create a new instance
		//
		Game3DEnvironment* env = new Game3DEnvironment;

		// Set the global environment before we do anything else because other modules might rely on it
		//
		g_pEnv = reinterpret_cast<Game3DEnvironment*>(env);		

		env->_classRegistry = new ClassRegistry;
		env->_classRegistry->RegisterAllClasses();

		// Set up the file system
		//
		env->_fileSystem = new FileSystem(L"EngineData");	
		env->_fileSystem->SetBaseDirectory(fs::current_path());		

		// Create the log file
		//
		env->CreateLogFile(options);

		LOG_INFO("Creating HexEngine using Game3DEnvironment configuration");		

		
		// Copy the target window pointer
		//
		if(options.window != nullptr)
			env->_windows.push_back(options.window);

		// Create the time manager
		//
		env->_timeManager = new TimeManager;

		LOG_DEBUG("TimeManager was successfully created (0x%p)", env->_timeManager);

		// Create the resource system
		//
		env->_resourceSystem = new ResourceSystem;
		env->_resourceSystem->Create();
		env->_resourceSystem->AddFileSystem(env->_fileSystem);

		env->_inputSystem = new InputSystem;

		env->_commandManager = new CommandManager;
		env->_commandManager->Create();
		
		if (!HEX_HASFLAG(options.flags, GameOptions::GameOptions_NoRenderer))
		{
			if (options.window != nullptr)
			{
				env->_windowWidth = options.window->GetClientWidth();
				env->_windowHeight = options.window->GetClientHeight();
				env->_windowHandle = options.window->GetHandle();

				env->_inputSystem->Create(options.window->GetHandle());

				env->_inputSystem->SetMousePosition(options.window->GetClientWidth() / 2, options.window->GetClientHeight() / 2, true);
			}
		}

		env->_assetPackageManager = new AssetPackageManager;

		env->_pluginSystem = new PluginSystem;
		if (auto numPluginsLoaded = env->_pluginSystem->LoadAllPlugins(); numPluginsLoaded > 0)
		{
			LOG_INFO("Loaded %d plugins", numPluginsLoaded);
		}

		env->_meshLoader = new MeshLoader;

		env->_modelImporter = (IModelImporter*)env->_pluginSystem->CreateInterface(IModelImporter::InterfaceName);
		env->_modelImporter->Create();		

		env->_compressionProvider = (ICompressionProvider*)env->_pluginSystem->CreateInterface(ICompressionProvider::InterfaceName);
		env->_compressionProvider->Create();

		env->_physicsSystem = (IPhysicsSystem*)env->_pluginSystem->CreateInterface(IPhysicsSystem::InterfaceName);
		env->_physicsSystem->Create();

		env->_fontImporter = (IFontImporter*)env->_pluginSystem->CreateInterface(IFontImporter::InterfaceName);
		env->_fontImporter->Create();

		env->_navMeshProvider = (INavMeshProvider*)env->_pluginSystem->CreateInterface(INavMeshProvider::InterfaceName);
		env->_navMeshProvider->Create();

		//env->_scriptEngine = (IScriptEngine*)env->_pluginSystem->CreateInterface(IScriptEngine::InterfaceName);
		//env->_scriptEngine->Create();

		env->_meshPrimitives = new MeshPrimitives;

		env->_materialLoader = new MaterialLoader;

		//env->_streamlineProvider = (IStreamlineProvider*)env->_pluginSystem->CreateInterface(IStreamlineProvider::InterfaceName);
		//env->_streamlineProvider->Create();

		env->_denoiserProvider = (IDenoiserProvider*)env->_pluginSystem->CreateInterface(IDenoiserProvider::InterfaceName);
		env->_denoiserProvider->Create();

		// Create the graphics engine
		//
		if (!HEX_HASFLAG(options.flags, GameOptions::GameOptions_NoRenderer))
		{
			if (env->CreateGraphicsSystem(options) == false)
			{
				return nullptr;
			}

			env->_ssaoProvider = (ISSAOProvider*)env->_pluginSystem->CreateInterface(ISSAOProvider::InterfaceName);
			env->_ssaoProvider->Create();
		}

		// load the standard assets
#ifndef _DEBUG
		LOG_INFO("Loading standard asset package");
		env->_standardAssets = (AssetPackage*)env->_resourceSystem->LoadResource("AssetPackages/StandardAssets.pkg");
#endif

		env->_debugGui = new DebugGUI;

		env->_sceneManager = new SceneManager;
		env->_sceneRenderer = new SceneRenderer;
		env->_sceneRenderer->Create();

		env->_debugRenderer = new DebugRenderer;
		env->_debugRenderer->Create();

		env->_chunkManager = new ChunkManager;
		env->_audioManager = new AudioManager;
		//env->_audioManager->Create();

		env->_uiManager = new UIManager;

		if (options.window != nullptr)
		{
			env->_uiManager->Create(options.window->GetClientWidth(), options.window->GetClientHeight());
		}
		
		if (options.createIconService)
		{
			env->_iconService = new IconService;
			env->_iconService->Create(L"IconScene");
		}

		env->_commandManager->RegisterVar(&cl_simulationRate);
		env->_commandManager->RegisterVar(&cl_showfps);
		env->_commandManager->RegisterVar(&cl_forcelagframe);
		env->_commandManager->GetConsole()->Create();


		LOG_INFO("HexEngine setup complete");		

		// We can now set the engine to "running" mode so the main loop can proceed
		//
		env->_running = true;

		// create the change watch after _running becomes true otherwise the thread will exit immediately
		env->_fileSystem->CreateChangeNotifier(env->_fileSystem->GetDataDirectory());

#if USE_MULTITHREADED_PHYSICS == 1
		env->_physicsThread = std::thread([env]() {

			const float physicsSpeed = cl_simulationRate._val.f32;

			TimeManager physicsTime;
			physicsTime.SetTargetFps(physicsSpeed);

			if (env->IsPhysicsSystemEnabled())
			{
				env->_isPhysicsThreadActive = true;

				while (env->IsRunning())
				{
					physicsTime.FrameStart();

					const double timeStep = 1.0 / physicsSpeed;// cl_simulationRate._val.f32;

					while (physicsTime._accumulatedSimulationTime >= timeStep)
					{
						env->_physicsSystem->Simulate(timeStep);

						physicsTime._accumulatedSimulationTime -= timeStep;
						physicsTime._simulationTime += timeStep;
					}

					env->_timeManager->_interpolationFactor = physicsTime._accumulatedSimulationTime / timeStep;

					physicsTime.FrameEnd();
				}

				env->_isPhysicsThreadActive = false;
			}
			});
		env->_physicsThread.detach();
#endif

		return env;
	}

	/// <summary>
	/// Destroy this instance
	/// </summary>
	void Game3DEnvironment::Destroy()
	{
#if USE_MULTITHREADED_PHYSICS == 1
		// Wait for the physics thread to cease, if it is running
		while (_isPhysicsThreadActive)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
#endif

		SAFE_UNLOAD(_standardAssets);

		for(auto& extension : _gameExtensions)
		{
			extension->OnShutdown();
			delete extension;
		}

		if (_iconService)
		{
			_iconService->Destroy();
			SAFE_DELETE(_iconService);
		}

		// Delete all the interfaces that we used
		//
		if (_sceneManager)
		{
			_sceneManager->Destroy();
			SAFE_DELETE(_sceneManager);
		}		

		SAFE_DELETE(_meshLoader);

		_meshPrimitives->Destroy();
		SAFE_DELETE(_meshPrimitives);

		_sceneRenderer->Destroy();
		SAFE_DELETE(_sceneRenderer);

		SAFE_DELETE(_debugGui);
		
		SAFE_DELETE(_fileSystem);		

		_modelImporter->Destroy();
		SAFE_DELETE(_modelImporter);

		_ssaoProvider->Destroy();
		SAFE_DELETE(_ssaoProvider);

		_compressionProvider->Destroy();
		SAFE_DELETE(_compressionProvider);

		_fontImporter->Destroy();
		SAFE_DELETE(_fontImporter);

		_navMeshProvider->Destroy();
		SAFE_DELETE(_navMeshProvider);

		//_scriptEngine->Destroy();
		//SAFE_DELETE(_scriptEngine);

		SAFE_DELETE(_debugRenderer);
		SAFE_DELETE(_timeManager);
		SAFE_DELETE(_uiManager);
		SAFE_DELETE(_chunkManager);
		SAFE_DELETE(_audioManager);
		SAFE_DELETE(_classRegistry);
		SAFE_DELETE(_inputSystem);

		if (_physicsSystem)
		{
			_physicsSystem->Destroy();
			delete _physicsSystem;
		}

		_pluginSystem->UnloadAllPlugins();
		SAFE_DELETE(_pluginSystem);

		IInputLayout::Destroy();
		SAFE_DELETE(_graphicsDevice);

		SAFE_DELETE(_shaderLoader);

		if (_resourceSystem)
		{
			_resourceSystem->Destroy();
			SAFE_DELETE(_resourceSystem);
		}

		SAFE_DELETE(_commandManager);

		SAFE_DELETE(_logFile);
		

		// Lastly, delete the environment
		//
		SAFE_DELETE(g_pEnv);

		CoUninitialize();

#ifdef _DEBUG
		_CrtDumpMemoryLeaks();
#endif

#if ENABLE_MEMORY_LEAK_TRACKER
		gMemoryTracker.DumpMemoryLeaks();
#endif
	}

	/// <summary>
	/// Determines if the engine is still running
	/// </summary>
	/// <returns></returns>
	bool Game3DEnvironment::IsRunning()
	{
		return _running;
	}

	void Game3DEnvironment::FixedStep(float dt)
	{
#if USE_MULTITHREADED_PHYSICS == 0
		if (IsPhysicsSystemEnabled())
			_physicsSystem->Simulate(dt);
#endif

		_sceneManager->FixedUpdate(dt);

		for (auto& extension : _gameExtensions)
		{
			extension->OnFixedUpdate(dt);
		}

		_timeManager->_accumulatedSimulationTime -= dt;

		//if (_timeManager->_accumulatedSimulationTime < 0)
		//	_timeManager->_accumulatedSimulationTime = 0;

		_timeManager->_simulationTime += dt;
	}

	/// <summary>
	/// Run this engine instance
	/// </summary>
	void Game3DEnvironment::Run()
	{
		static bool hasUpdatedOnce = false;

		_timeManager->FrameStart(hasUpdatedOnce);


		MSG msg = { 0 };

		while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			if (msg.message == WM_QUIT)
			{
				OnRecieveQuitMessage();
				break;
			}

			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

		for (auto& window : _windows)
		{
			window->Update();
		}
		
		const float timeStep = 1.0f / cl_simulationRate._val.f32;	

		_inputSystem->Update(_timeManager->_frameTime);		

		while (_timeManager->_accumulatedSimulationTime >= timeStep)
		{
			_sceneManager->FixedUpdate(timeStep);

			for (auto& extension : _gameExtensions)
			{
				extension->OnFixedUpdate(timeStep);
			}

#if USE_MULTITHREADED_PHYSICS == 0
			if (IsPhysicsSystemEnabled())
			{
				_physicsSystem->Simulate(timeStep);
				_physicsSystem->Update(timeStep);
			}
#endif	

			_timeManager->_accumulatedSimulationTime -= timeStep;
			_timeManager->_simulationTime += timeStep;

			hasUpdatedOnce = true;
		}

		if(GetHasFocus() == false)
			std::this_thread::sleep_for(std::chrono::milliseconds(50));

		if (hasUpdatedOnce)
		{
			if (hasUpdatedOnce)
			{
#if USE_MULTITHREADED_PHYSICS == 0
				// Calculate the interpolation factor
				//
				_timeManager->_interpolationFactor = std::clamp(_timeManager->_accumulatedSimulationTime / timeStep, 0.0f, 1.0f);
#endif

				_audioManager->Update();

				_uiManager->Update(_timeManager->_frameTime);

				_sceneManager->Update(_timeManager->_frameTime);				

				_resourceSystem->Update();

				for (auto& extension : _gameExtensions)
				{
					extension->OnUpdate(_timeManager->_frameTime);
				}

				bool shouldUseDepthBuffer = true;

				if (auto currentScene = _sceneManager->GetCurrentScene(); currentScene != nullptr)
				{
					if (auto mainCamera = currentScene->GetMainCamera(); mainCamera != nullptr)
					{
						shouldUseDepthBuffer = !mainCamera->IsDLSSEnabled();
					}
				}

				for (auto& win : _windows)
				{
					_graphicsDevice->BeginFrame(win, shouldUseDepthBuffer ? _sceneRenderer->GetGBuffer()->GetDepthBuffer() : nullptr);
					{
						if (_iconService)
							_iconService->Render();

						_sceneManager->Render();

						if (_iconService)
							_iconService->CompletedFrame();

						g_pEnv->_graphicsDevice->SetRenderTarget(g_pEnv->_graphicsDevice->GetBackBuffer());

						// Restore the viewport back to the backbuffer's, we need this incase DLSS is on
						auto graphics = g_pEnv->_graphicsDevice;
						graphics->SetViewport(graphics->GetBackBufferViewport());



						_uiManager->GetRenderer()->StartFrame();
						_uiManager->GetRenderer()->EnableScaling(true);

						_graphicsDevice->SetBlendState(BlendState::Transparency);

						if (!_inEditorMode)
							_uiManager->GetRenderer()->FullScreenTexturedQuad(_sceneManager->GetCurrentScene()->GetMainCamera()->GetRenderTarget());

						if (cl_showfps._val.b)
						{
							_uiManager->GetRenderer()->PrintText(_uiManager->GetRenderer()->_style.font.get(), (uint8_t)Style::FontSize::Small, 5, 5, math::Color(1, 1, 1, 1), 0, std::format(L"FPS: {:d}", _timeManager->_fps));
						}

						//_sceneRenderer->RenderOverlays(SceneFlags::PostProcessingEnabled);

						_sceneManager->GetCurrentScene()->OnGUI();

						_uiManager->Render();

						for (auto& extension : _gameExtensions)
						{
							extension->OnGUI();
						}

						if (auto console = _commandManager->GetConsole(); console != nullptr)
						{
							if (console->GetActive())
								console->Render(_uiManager->GetRenderer());
						}

						if (_debugGui)
							_debugGui->Render();

						_uiManager->GetRenderer()->EnableScaling(false);
						_uiManager->GetRenderer()->EndFrame();


					}
					_graphicsDevice->EndFrame(win);
				}

				_sceneManager->LateUpdate(_timeManager->_frameTime);

				if (cl_forcelagframe._val.i32 > 0)
					std::this_thread::sleep_for(std::chrono::milliseconds(cl_forcelagframe._val.i32));

				/*if (_timeManager->_frameCount >= 500)
				{
					OnRecieveQuitMessage();
				}*/
			}
		}
		else
		{
			
		}

		//_sceneManager->LateUpdate(_timeManager->_frameTime);

		_timeManager->FrameEnd();
	}

	void Game3DEnvironment::SetEditorMode(bool editorMode)
	{
		_inEditorMode = editorMode;
	}

	/// <summary>
	/// A quit message was received
	/// </summary>
	void Game3DEnvironment::OnRecieveQuitMessage()
	{
		LOG_INFO("A quit signal was recieved, ending the session");

		_running = false;
	}

	/// <summary>
	/// Create the graphics subsystem by loading the relevant module into the engine and calling it's entry point
	/// </summary>
	/// <param name="engine">That graphics engine we wish to use</param>
	/// <returns></returns>
	bool Game3DEnvironment::CreateGraphicsSystem(const Game3DOptions& options)
	{
		_shaderLoader = new ShaderSystem;

		_graphicsDevice = (IGraphicsDevice*)_pluginSystem->CreateInterface(IGraphicsDevice::InterfaceName);

		if (_graphicsDevice == nullptr)
		{
			return false;
		}

		if (_graphicsDevice->Create() == false)
		{
			SAFE_DELETE(_graphicsDevice);
			return false;
		}

		std::vector<ScreenDisplayMode> displayModes;
		if (_graphicsDevice->GetSupportedDisplayModes(displayModes))
		{
			for (auto&& mode : displayModes)
			{
				LOG_DEBUG("Found display mode %dx%d @ %dhz", mode.width, mode.height, mode.refresh.numerator / mode.refresh.denominator);
			}
		}

		if (options.window != nullptr)
		{
			if (_graphicsDevice->AttachToWindow(options.window) == false)
			{
				return false;
			}
		}
		else
		{
			LOG_CRIT("No suitable graphics window was supplied, exiting!");
			return false;
		}

		return true;
	}

	void Game3DEnvironment::CreateLogFile(const Game3DOptions& options)
	{
		// Initialise a new log file instance
		//
		std::wstring logName = L"Logs/LogFile_" + options.applicationName + L".txt";
		_logFile = new LogFile(_fileSystem->GetLocalAbsolutePath(logName), LogOptions::IncludeTime, LogLevel::Crit);

		// Output our standard header
		//
		_logFile->WriteLine(LogLevel::Info, "**********************************************************************");
		_logFile->WriteLine(LogLevel::Info, "**  HexEngine v%d.%d", GET_MAJOR_VERSION(HexEngineVersion), GET_MINOR_VERSION(HexEngineVersion));
		_logFile->WriteLine(LogLevel::Info, "**  Built on %s %s", __DATE__, __TIME__);
		_logFile->WriteLine(LogLevel::Info, "**********************************************************************");
	}

	float Game3DEnvironment::GetAspectRatio()
	{
		return (float)_windowWidth / (float)_windowHeight;
	}

	float Game3DEnvironment::GetScreenScaleX() const
	{
		return (float)_windowWidth / (float)DEV_RESOLUTION_X;
	}

	float Game3DEnvironment::GetScreenScaleY() const
	{
		return (float)_windowHeight / (float)DEV_RESOLUTION_Y;
	}

	int32_t Game3DEnvironment::GetScreenWidth() const
	{
		return _windowWidth;
	}

	int32_t Game3DEnvironment::GetScreenHeight() const
	{
		return _windowHeight;
	}

	void Game3DEnvironment::OnResizeWindow(Window* window, uint32_t width, uint32_t height)
	{
		LOG_DEBUG("Resizing game window to %dx%d", width, height);

		_windowWidth = width;
		_windowHeight = height;

		if (width > 0 && height > 0)
		{
			if (_graphicsDevice)
				_graphicsDevice->Resize(window, width, height);

			if (_sceneRenderer)
				_sceneRenderer->Resize(width, height);
		}

		if (_sceneManager)
		{
			if (auto currentScene = _sceneManager->GetCurrentScene(); currentScene != nullptr)
			{
				if (auto mainCamera = currentScene->GetMainCamera(); mainCamera != nullptr)
				{
					mainCamera->SetPespectiveParameters(mainCamera->GetFov(), GetAspectRatio(), mainCamera->GetNearZ(), mainCamera->GetFarZ());

					mainCamera->SetViewport(math::Viewport(0.0f, 0.0f, (float)_windowWidth, (float)_windowHeight));
				}
			}
		}

		if (_uiManager)
		{
			_uiManager->Resize(width, height);
		}

		if (_debugGui)
		{
			_debugGui->Resize(_windowHandle);
		}

		for (auto& extension : _gameExtensions)
		{
			extension->OnResize(width, height);
		}
	}

	void Game3DEnvironment::GetScreenSize(uint32_t& width, uint32_t& height) const
	{
		width = _windowWidth;
		height = _windowHeight;
	}

	bool Game3DEnvironment::GetHasFocus() const
	{
		return _hasFocus;
	}

	void Game3DEnvironment::SetHasFocus(bool hasFocus)
	{
		_hasFocus = hasFocus;
	}

	bool Game3DEnvironment::IsEditorMode() const
	{
		return _inEditorMode;
	}
}