
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
#include "../Graphics/DiffuseGIAOProvider.hpp"
#include "../Input/Hvar.hpp"
#include "../Terrain/ChunkManager.hpp"
#include "../Plugin/PluginSystem.hpp"
#include "../GUI/UIManager.hpp"
#include "../Graphics/IconService.hpp"
#include "../Scripting/IScriptEngine.hpp"
#include "../Graphics/IDenoiserProvider.hpp"
#include "../Steam/ISteamworksProvider.hpp"
#include "../Graphics/MeshLoader.hpp"
#include "../FileSystem/PrefabLoader.hpp"
#include "../FileSystem/ParticleEffectLoader.hpp"
#include "../Scene/ParticleWorldSystem.hpp"

#define USE_MULTITHREADED_PHYSICS 0


namespace HexEngine
{
	HVar cl_simulationRate("cl_simulationRate", "The speed at which physics is simulated in hz", 120.0f, 1.0f, 240.0f);
	HVar cl_showfps("cl_showfps", "Draw the current frames per-second on the screen", false, false, true);
	HVar cl_forcelagframe("cl_forcelagframe", "Forcefully delay each frame by this amount (in milliseconds)", 0, 0, 1000);
	HVar r_debugFrameInfo("r_debugFrameInfo", "Draws per-frame renderer diagnostics (weather, HDR, SSR/TAA state, camera) as text overlay", false, false, true);

	Game3DEnvironment::Game3DEnvironment() :
		_running(false)
	{
	}

	/**
	 * @brief Creates and initializes a full 3D runtime environment.
	 * @param options Startup options (window, flags, app metadata).
	 * @return Initialized environment instance, or `nullptr` on failure.
	 */
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
		// load the standard assets
		// Create the resource system
		//
		env->_resourceSystem = new ResourceSystem;
		env->_resourceSystem->Create();

		env->_assetPackageManager = new AssetPackageManager;

		if (fs::exists(".\\Data\\AssetPackages\\EngineAssets.pkg"))
		{
			FileSystem tempFs(L"EngineDataBootStrap");
			tempFs.SetBaseDirectory(fs::current_path());
			env->_resourceSystem->AddFileSystem(&tempFs);

			LOG_INFO("Loading standard asset package");
			env->_standardAssets = AssetPackage::Create("EngineDataBootStrap.AssetPackages/EngineAssets.pkg", L"EngineData");

			if(env->_standardAssets)
				env->_fileSystem = env->_standardAssets.get();

			env->_resourceSystem->RemoveFileSystem(&tempFs);
			// AssetPackage::Create already calls AddFileSystem internally; do
			// NOT add env->_fileSystem again below (this used to show up as a
			// double "EngineData" entry in the resource system's mount list
			// until the AddFileSystem dedup was added; the dedup catches it
			// now but the second call was still pointless).
		}
		else
		{
			env->_fileSystem = new FileSystem(L"EngineData");
			env->_fileSystem->SetBaseDirectory(fs::current_path());
			// Disk-based fallback was never registered with ResourceSystem
			// by anyone else, so add it here. Pkg-loaded path already did.
			env->_resourceSystem->AddFileSystem(env->_fileSystem);
		}

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

		// (AddFileSystem for env->_fileSystem now happens above, inside whichever
		// branch created/loaded it - pkg path via AssetPackage::Create, loose-
		// data path via the explicit else branch. The previous unconditional
		// add here double-registered the pkg.)

		env->_inputSystem = new InputSystem;

		env->_commandManager = new CommandManager;
		env->_commandManager->Create();

		env->_prefabLoader = new PrefabLoader;
		env->_particleEffectLoader = new ParticleEffectLoader;
		
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

		// Steamworks integration - optional. TryCreateInterface returns null
		// silently when no plugin provides it OR when running outside Steam
		// (no app launched via Steam = SteamAPI_Init fails inside the plugin
		// and Create returns false - we surface that as "not initialised" via
		// IsInitialised() rather than leaving a half-dead provider around).
		env->_steamworksProvider = (ISteamworksProvider*)env->_pluginSystem->TryCreateInterface(ISteamworksProvider::InterfaceName);
		if (env->_steamworksProvider != nullptr)
		{
			if (!env->_steamworksProvider->Create())
			{
				// Steam isn't running / app id mismatch / DRM rejected. Drop the
				// provider so game code's "if (_steamworksProvider != null)"
				// guards do the right thing.
				env->_steamworksProvider->Destroy();
				delete env->_steamworksProvider;
				env->_steamworksProvider = nullptr;
			}
		}

		// Create the graphics engine
		//
		if (!HEX_HASFLAG(options.flags, GameOptions::GameOptions_NoRenderer))
		{
			if (env->CreateGraphicsSystem(options) == false)
			{
				return nullptr;
			}

			// TryCreateInterface (vs CreateInterface): silently returns null
			// when no plugin SSAO is loaded, so we can fall back to the
			// in-engine DiffuseGIAOProvider without raising the modal
			// "Critical Error" dialog that LOG_CRIT pops up.
			env->_ssaoProvider = (ISSAOProvider*)env->_pluginSystem->TryCreateInterface(ISSAOProvider::InterfaceName);
			if (env->_ssaoProvider == nullptr)
			{
				// The provider pulls AO from the DiffuseGI volume's voxel cone
				// trace alpha so we get a free AO signal without a dedicated
				// screen-space pass. _gi pointer is filled in lazily on first
				// ApplyAmbientOcclusion call - SceneRenderer (and hence
				// _diffuseGi) doesn't exist yet at this point in startup.
				env->_ssaoProvider = new DiffuseGIAOProvider(nullptr);
			}
			env->_ssaoProvider->Create();
		}

		env->_debugGui = new DebugGUI;

		env->_sceneManager = new SceneManager;
		env->_sceneRenderer = new SceneRenderer;
		env->_sceneRenderer->Create();

		env->_particleWorldSystem = new ParticleWorldSystem;
		env->_particleWorldSystem->Create();

		env->_debugRenderer = new DebugRenderer;
		env->_debugRenderer->Create();

		env->_chunkManager = new ChunkManager;
		env->_audioManager = new AudioManager;
		env->_audioManager->Create();

		env->_uiManager = new UIManager;
		env->_hdrPresentShader = IShader::Create("EngineData.Shaders/PresentHDR.hcs");

		if (options.window != nullptr)
		{
			env->_uiManager->Create(options.window->GetClientWidth(), options.window->GetClientHeight());
		}
		
		if (options.createIconService)
		{
			env->_iconService = new IconService;
			env->_iconService->Create(L"IconScene");
		}

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

	/** @brief Destroys this environment instance and all owned subsystems. */
	void Game3DEnvironment::Destroy()
	{
#if USE_MULTITHREADED_PHYSICS == 1
		// Wait for the physics thread to cease, if it is running
		while (_isPhysicsThreadActive)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
#endif

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

		_meshPrimitives->Destroy();
		SAFE_DELETE(_meshPrimitives);

		_sceneRenderer->Destroy();
		SAFE_DELETE(_sceneRenderer);

		if (_particleWorldSystem)
		{
			_particleWorldSystem->Destroy();
			SAFE_DELETE(_particleWorldSystem);
		}

		SAFE_DELETE(_meshLoader);

		SAFE_DELETE(_debugGui);
		
		SAFE_DELETE(_fileSystem);		

		_modelImporter->Destroy();
		SAFE_DELETE(_modelImporter);

		_ssaoProvider->Destroy();
		SAFE_DELETE(_ssaoProvider);

		// Steamworks may have been declined at Create() (no Steam running etc)
		// so the provider pointer is null on the no-steam path. Only release
		// when we actually got an instance.
		if (_steamworksProvider != nullptr)
		{
			_steamworksProvider->Destroy();
			SAFE_DELETE(_steamworksProvider);
		}

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
		SAFE_DELETE(_prefabLoader);
		SAFE_DELETE(_particleEffectLoader);

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

	/** @brief Returns whether the environment main loop is still running. */
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

	/** @brief Executes one frame of the game environment main loop. */
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

		// Steamworks callbacks. Must run on the main thread once per frame
		// BEFORE any other Steam call so overlay-activated / achievement /
		// stat callbacks have a chance to fire. Calling later in the frame
		// would still pump them but they'd be one frame stale for any code
		// that read provider state during the frame.
		if (_steamworksProvider != nullptr)
			_steamworksProvider->Tick();

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
						{
							auto presentShader = (IShader*)nullptr;
							if (auto backBuffer = _graphicsDevice->GetBackBuffer(); backBuffer != nullptr && backBuffer->GetFormat() == DXGI_FORMAT_R16G16B16A16_FLOAT)
							{
								presentShader = _hdrPresentShader.get();
							}

							_uiManager->GetRenderer()->FullScreenTexturedQuad(_sceneManager->GetCurrentScene()->GetMainCamera()->GetRenderTarget(), presentShader);
						}

						//_sceneRenderer->RenderOverlays(SceneFlags::PostProcessingEnabled);

						_uiManager->Render();

						_sceneManager->GetCurrentScene()->OnGUI();						

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

						if (cl_showfps._val.b)
						{
							_uiManager->GetRenderer()->PrintText(_uiManager->GetRenderer()->_style.font.get(), (uint8_t)Style::FontSize::Small, 5, 5, math::Color(1, 1, 1, 1), 0, std::format(L"FPS: {:d}", _timeManager->_fps));
						}

						// Side-by-side diagnostic overlay - sample any per-frame
						// renderer inputs that legitimately can diverge between
						// editor and launcher (weather state, HDR calibration,
						// SSR/TAA toggles, camera state) and draw them as text.
						// Enables quick visual A/B without needing a graphics
						// debugger attached, which has been intermittently
						// failing to capture this engine's swap chain.
						if (r_debugFrameInfo._val.b)
						{
							auto* renderer = _uiManager->GetRenderer();
							auto* font = renderer->_style.font.get();
							const uint8_t fs = (uint8_t)Style::FontSize::Small;
							const math::Color col(1.0f, 1.0f, 0.6f, 1.0f);

							int32_t y = 30;
							auto line = [&](const std::wstring& s)
							{
								renderer->PrintText(font, fs, 5, y, col, 0, s);
								y += 14;
							};

							line(std::format(L"-- r_debugFrameInfo --"));
							line(std::format(L"editorMode: {}", _inEditorMode ? L"true" : L"false"));

							uint32_t sw = 0, sh = 0;
							_graphicsDevice->GetBackBufferDimensions(sw, sh);
							line(std::format(L"backbuffer: {}x{}", sw, sh));
							if (auto* bb = _graphicsDevice->GetBackBuffer(); bb != nullptr)
							{
								line(std::format(L"backbuffer fmt: {} (hdr={})",
									(int32_t)bb->GetFormat(),
									bb->GetFormat() == DXGI_FORMAT_R16G16B16A16_FLOAT ? L"yes" : L"no"));
							}

							// HDR HVars - easy lookups by name to avoid extern wiring.
							auto getf = [this](const char* n) -> float {
								if (auto* h = _commandManager->FindHVar(n)) return h->_val.f32;
								return -1.0f;
							};
							auto getb = [this](const char* n) -> bool {
								if (auto* h = _commandManager->FindHVar(n)) return h->_val.b;
								return false;
							};
							auto geti = [this](const char* n) -> int32_t {
								if (auto* h = _commandManager->FindHVar(n)) return h->_val.i32;
								return -1;
							};

							line(std::format(L"r_hdrOutput: {}", getb("r_hdrOutput") ? L"1" : L"0"));
							line(std::format(L"r_hdrPaperWhiteNits: {:.1f}", getf("r_hdrPaperWhiteNits")));
							line(std::format(L"r_hdrPeakNits: {:.1f}", getf("r_hdrPeakNits")));
							line(std::format(L"r_ssr: {}  r_ssrDenoise: {}  r_taa: {}",
								getb("r_ssr") ? L"1" : L"0",
								getb("r_ssrDenoise") ? L"1" : L"0",
								geti("r_taa")));
							line(std::format(L"r_exposure: {:.2f}  r_autoExposureMax: {:.1f}",
								getf("r_exposure"), getf("r_autoExposureMax")));

							if (auto scene = _sceneManager ? _sceneManager->GetCurrentScene() : nullptr; scene != nullptr)
							{
								const auto& wsp = scene->GetWeatherSurfaceParams();
								line(std::format(L"weather wetness: {:.3f}  puddle: {:.3f}",
									wsp.wetness, wsp.puddleAmount));
								line(std::format(L"weather precip: {:.3f}  snow: {:.3f}",
									wsp.precipitationIntensity, wsp.snowCoverage));

								if (auto cam = scene->GetMainCamera(); cam != nullptr && cam->GetEntity() != nullptr)
								{
									auto p = cam->GetEntity()->GetPosition();
									line(std::format(L"camera pos: ({:.4f}, {:.4f}, {:.4f})", p.x, p.y, p.z));

									// Per-frame delta - if the player is "stationary" but
									// the camera entity is creeping by gravity / physics /
									// lerp smoothing, NRD reads the velocity buffer as
									// scene motion and accumulates a reprojection trail.
									// Shows micro-motion the user can't see directly.
									static math::Vector3 sPrevCamPos = p;
									const math::Vector3 d = p - sPrevCamPos;
									sPrevCamPos = p;
									line(std::format(L"camera delta: ({:.6f}, {:.6f}, {:.6f}) |d|={:.6f}",
										d.x, d.y, d.z, d.Length()));

									const auto& vp = cam->GetViewport();
									line(std::format(L"camera viewport: {}x{}", (int32_t)vp.width, (int32_t)vp.height));
									line(std::format(L"camera fov: {:.3f}", cam->GetFov()));
								}
							}

							line(std::format(L"frame: {}", _timeManager ? _timeManager->_frameCount : 0u));
						}

						_uiManager->GetRenderer()->EnableScaling(false);
						_uiManager->GetRenderer()->EndFrame();


					}
					_graphicsDevice->EndFrame(win);
				}

				_sceneManager->LateUpdate(_timeManager->_frameTime);

				if (cl_forcelagframe._val.i32 > 0)
					std::this_thread::sleep_for(std::chrono::milliseconds(cl_forcelagframe._val.i32));
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

	/** @brief Handles a quit request and stops the environment loop. */
	void Game3DEnvironment::OnRecieveQuitMessage()
	{
		LOG_INFO("A quit signal was recieved, ending the session");

		_running = false;
	}

	/**
	 * @brief Creates and initializes the graphics backend subsystem.
	 * @param options Startup options containing the target window.
	 * @return True when graphics initialization succeeds.
	 */
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
		if (!_fileSystem)
			return;

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
