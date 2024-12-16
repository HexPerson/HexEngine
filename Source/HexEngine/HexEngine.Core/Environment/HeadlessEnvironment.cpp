
#include "HeadlessEnvironment.hpp"
#include "../FileSystem/FileSystem.hpp"
#include "TimeManager.hpp"
#include "LogFile.hpp"
#include "../FileSystem/KeyValues.hpp"
#include "../FileSystem/ResourceSystem.hpp"
#include "../FileSystem/ICompressionProvider.hpp"
#include "../Graphics/ShaderSystem.hpp"
#include "../Graphics/MaterialLoader.hpp"
#include "../Scene/SceneManager.hpp"
#include "../Input/InputSystem.hpp"
#include "../Graphics/DebugRenderer.hpp"
#include "../Graphics/ISSAOProvider.hpp"
#include "../Input/Hvar.hpp"
#include "../Terrain/ChunkManager.hpp"
#include "../Plugin/PluginSystem.hpp"
#include "../Physics/IPhysicsSystem.hpp"

#define USE_MULTITHREADED_PHYSICS 0

namespace HexEngine
{
	extern HVar cl_simulationRate;

	HeadlessEnvironment::HeadlessEnvironment() :
		_running(false)
	{}

	/// <summary>
	/// Create a 3D game environment
	/// </summary>
	/// <param name="options">The engine options</param>
	/// <returns>A pointer to a Game3DEnvironment instance</returns>
	HeadlessEnvironment* HeadlessEnvironment::Create(const HeadlessOptions& options)
	{
		CoInitializeEx(NULL, COINIT_MULTITHREADED);

		// Create a new instance
		//
		HeadlessEnvironment* env = new HeadlessEnvironment;

		// Set the global environment before we do anything else because other modules might rely on it
		//
		g_pEnv = reinterpret_cast<HeadlessEnvironment*>(env);

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



		if (options.gameExtension != nullptr)
		{
			env->AddGameExtension(options.gameExtension);
			LOG_DEBUG("IGameExtension has been provided: %p", options.gameExtension);
		}

		// Create the time manager
		//
		env->_timeManager = new TimeManager;

		// Create the resource system
		//
		env->_resourceSystem = new ResourceSystem;
		env->_resourceSystem->Create();

		env->_pluginSystem = new PluginSystem;
		if (auto numPluginsLoaded = env->_pluginSystem->LoadAllPlugins(); numPluginsLoaded > 0)
		{
			LOG_INFO("Loaded %d plugins", numPluginsLoaded);
		}

		env->_physicsSystem = (IPhysicsSystem*)env->_pluginSystem->CreateInterface(IPhysicsSystem::InterfaceName);
		env->_physicsSystem->Create();

		env->_compressionProvider = (ICompressionProvider*)env->_pluginSystem->CreateInterface(ICompressionProvider::InterfaceName);
		env->_compressionProvider->Create();

		//env->_testModel = env->_modelSystem->LoadFromDisk(env->_fileSystem->GetLocalAbsolutePath(L"Models/Sponza/Sponza.obj"));

		LOG_INFO("HexEngine setup complete");

		if (options.gameExtension)
			options.gameExtension->OnCreateGame();

		// We can now set the engine to "running" mode so the main loop can proceed
		//
		env->_running = true;

#if USE_MULTITHREADED_PHYSICS == 1
		env->_physicsThread = std::thread([env]() {

			const float physicsSpeed = 60.0f;

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
						env->_physicsSystem->Update(timeStep);

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
	void HeadlessEnvironment::Destroy()
	{
#if USE_MULTITHREADED_PHYSICS == 1
		// Wait for the physics thread to cease, if it is running
		while (_isPhysicsThreadActive)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
#endif



		for (auto& extension : _gameExtensions)
		{
			extension->OnShutdown();
		}

		// Delete all the interfaces that we used
		//
		SAFE_DELETE(_fileSystem);

		_compressionProvider->Destroy();
		SAFE_DELETE(_compressionProvider);
		SAFE_DELETE(_timeManager);
		SAFE_DELETE(_classRegistry);

		if (_physicsSystem)
		{
			_physicsSystem->Destroy();
			delete _physicsSystem;
		}

		_pluginSystem->UnloadAllPlugins();
		SAFE_DELETE(_pluginSystem);

		if (_resourceSystem)
		{
			_resourceSystem->Destroy();
			SAFE_DELETE(_resourceSystem);
		}

		SAFE_DELETE(_logFile);


		// Lastly, delete the environment
		//
		SAFE_DELETE(g_pEnv);

		CoUninitialize();

#if ENABLE_MEMORY_LEAK_TRACKER
		gMemoryTracker.DumpMemoryLeaks();
#endif
	}

	/// <summary>
	/// Determines if the engine is still running
	/// </summary>
	/// <returns></returns>
	bool HeadlessEnvironment::IsRunning()
	{
		return _running;
	}

	/// <summary>
	/// Run this engine instance
	/// </summary>
	void HeadlessEnvironment::Run()
	{
		static bool hasUpdatedOnce = false;

		_timeManager->FrameStart(hasUpdatedOnce);

		const float timeStep = 1.0f / cl_simulationRate._val.f32;

		

		while (_timeManager->_accumulatedSimulationTime >= timeStep /*|| _timeManager->_accumulatedSimulationTime == 0.0f*/)
		{
			for (auto& extension : _gameExtensions)
			{
				extension->OnFixedUpdate(timeStep);
			}

			_timeManager->_accumulatedSimulationTime -= timeStep;

			//if (_timeManager->_accumulatedSimulationTime < 0)
			//	_timeManager->_accumulatedSimulationTime = 0;

			_timeManager->_simulationTime += timeStep;

			hasUpdatedOnce = true;
		}


		if (GetHasFocus())
		{
			if (hasUpdatedOnce)
			{
#if USE_MULTITHREADED_PHYSICS == 0
				if (IsPhysicsSystemEnabled() && _timeManager->_frameTime > 0.0f)
					_physicsSystem->Update(_timeManager->_frameTime);
#endif

				// Calculate the interpolation factor
				//
				//_timeManager->_interpolationFactor = _timeManager->_accumulatedSimulationTime / timeStep;
				_resourceSystem->Update();

				for (auto& extension : _gameExtensions)
				{
					extension->OnUpdate(_timeManager->_frameTime);
				}
			}

			_inputSystem->Update(_timeManager->_frameTime);
		}

		//_sceneManager->LateUpdate(_timeManager->_frameTime);

		_timeManager->FrameEnd();
	}

	/// <summary>
	/// A quit message was received
	/// </summary>
	void HeadlessEnvironment::OnRecieveQuitMessage()
	{
		LOG_INFO("A quit signal was recieved, ending the session");

		_running = false;
	}

	void HeadlessEnvironment::CreateLogFile(const HeadlessOptions& options)
	{
		// Initialise a new log file instance
		//
		_logFile = new LogFile(_fileSystem->GetLocalAbsolutePath(L"Logs/LogFile.txt"), LogOptions::IncludeTime, LogLevel::Crit);

		// Output our standard header
		//
		_logFile->WriteLine(LogLevel::Info, "**********************************************************************");
		_logFile->WriteLine(LogLevel::Info, "**  HexEngine v%d.%d", GET_MAJOR_VERSION(HexEngineVersion), GET_MINOR_VERSION(HexEngineVersion));
		_logFile->WriteLine(LogLevel::Info, "**  Built on %s %s", __DATE__, __TIME__);
		_logFile->WriteLine(LogLevel::Info, "**********************************************************************");
	}

	float HeadlessEnvironment::GetAspectRatio()
	{
		return 1.0f;
	}

	void HeadlessEnvironment::OnResizeWindow(uint32_t width, uint32_t height, HWND handle)
	{
	}

	void HeadlessEnvironment::GetScreenSize(uint32_t& width, uint32_t& height) const
	{
		width = 0;
		height = 0;
	}

	bool HeadlessEnvironment::GetHasFocus() const
	{
		return _hasFocus;
	}

	void HeadlessEnvironment::SetHasFocus(bool hasFocus)
	{
		_hasFocus = hasFocus;
	}

	float HeadlessEnvironment::GetScreenScaleX() const
	{
		return 0.0f;
	}

	float HeadlessEnvironment::GetScreenScaleY() const
	{
		return 0.0f;
	}

	int32_t HeadlessEnvironment::GetScreenWidth() const
	{
		return 0;
	}

	int32_t HeadlessEnvironment::GetScreenHeight() const
	{
		return 0;
	}
}