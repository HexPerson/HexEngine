

#pragma once

#include "../Required.hpp"
#include "../Graphics/MeshLoader.hpp"
#include "../Graphics/Window.hpp"

namespace HexEngine
{
	#define DEV_RESOLUTION_X 3840
	#define DEV_RESOLUTION_Y 2081 //2160 2081 is when in windowed editor mode

	class IGameExtension;
	class IGraphicsDevice;
	class SceneManager;
	class InputSystem;
	class UIManager;
	class LogFile;

	/**
	 * @brief Core runtime environment interface and global service hub.
	 *
	 * Concrete environment implementations own the engine main loop,
	 * window lifecycle, subsystem creation, and active game extensions.
	 */
	class HEX_API IEnvironment
	{
	public:
		/**
		 * @brief Destroys a concrete environment instance.
		 * @param environment Environment pointer created by the engine bootstrap.
		 */
		static void DestroyEnvironment(IEnvironment* environment);

		/** @brief Returns whether a physics provider is currently available. */
		bool IsPhysicsSystemEnabled()
		{
			return _physicsSystem != nullptr;
		}

		FileSystem& GetFileSystem()
		{
			HEX_ASSERT(_fileSystem != nullptr);
			return *_fileSystem;
		}

		const FileSystem& GetFileSystem() const
		{
			HEX_ASSERT(_fileSystem != nullptr);
			return *_fileSystem;
		}

		IGraphicsDevice& GetGraphicsDevice()
		{
			HEX_ASSERT(_graphicsDevice != nullptr);
			return *_graphicsDevice;
		}

		const IGraphicsDevice& GetGraphicsDevice() const
		{
			HEX_ASSERT(_graphicsDevice != nullptr);
			return *_graphicsDevice;
		}

		ResourceSystem& GetResourceSystem()
		{
			HEX_ASSERT(_resourceSystem != nullptr);
			return *_resourceSystem;
		}

		const ResourceSystem& GetResourceSystem() const
		{
			HEX_ASSERT(_resourceSystem != nullptr);
			return *_resourceSystem;
		}

		SceneManager& GetSceneManager()
		{
			HEX_ASSERT(_sceneManager != nullptr);
			return *_sceneManager;
		}

		const SceneManager& GetSceneManager() const
		{
			HEX_ASSERT(_sceneManager != nullptr);
			return *_sceneManager;
		}

		InputSystem& GetInputSystem()
		{
			HEX_ASSERT(_inputSystem != nullptr);
			return *_inputSystem;
		}

		const InputSystem& GetInputSystem() const
		{
			HEX_ASSERT(_inputSystem != nullptr);
			return *_inputSystem;
		}

		UIManager& GetUIManager()
		{
			HEX_ASSERT(_uiManager != nullptr);
			return *_uiManager;
		}

		const UIManager& GetUIManager() const
		{
			HEX_ASSERT(_uiManager != nullptr);
			return *_uiManager;
		}

		void SetUIManager(UIManager* uiManager);

		LogFile& GetLogFile()
		{
			HEX_ASSERT(_logFile != nullptr);
			return *_logFile;
		}

		const LogFile& GetLogFile() const
		{
			HEX_ASSERT(_logFile != nullptr);
			return *_logFile;
		}

		const std::vector<IGameExtension*>& GetGameExtensions() const
		{
			return _gameExtensions;
		}

		// Legacy direct access members
		
		class IGraphicsDevice* _graphicsDevice = nullptr;
		class TimeManager* _timeManager = nullptr;
		class LogFile* _logFile = nullptr;
		class IModelImporter* _modelImporter = nullptr;
		
		class ShaderSystem* _shaderLoader = nullptr;
		class SceneManager* _sceneManager = nullptr;
		class SceneRenderer* _sceneRenderer = nullptr;
		class MeshPrimitives* _meshPrimitives = nullptr;		
		class InputSystem* _inputSystem = nullptr;
		std::vector<Window*> _windows;
		class DebugRenderer* _debugRenderer = nullptr;
		class IPhysicsSystem* _physicsSystem = nullptr;
		class IFontImporter* _fontImporter = nullptr;
		class DebugGUI* _debugGui = nullptr;
		class CommandManager* _commandManager = nullptr;
		class ChunkManager* _chunkManager = nullptr;
		class AudioManager* _audioManager = nullptr;
		class ClassRegistry* _classRegistry = nullptr;
		class MaterialLoader* _materialLoader = nullptr;
		class PluginSystem* _pluginSystem = nullptr;
		class ISSAOProvider* _ssaoProvider = nullptr;
		class ICompressionProvider* _compressionProvider = nullptr;
		
		class AssetPackageManager* _assetPackageManager = nullptr;
		class IconService* _iconService = nullptr;
		//class IScriptEngine* _scriptEngine = nullptr; // disabled currently
		class IStreamlineProvider* _streamlineProvider = nullptr;
		class IDenoiserProvider* _denoiserProvider = nullptr;
		class INavMeshProvider* _navMeshProvider = nullptr;
		MeshLoader* _meshLoader = nullptr;
		class PrefabLoader* _prefabLoader = nullptr;
		class ParticleEffectLoader* _particleEffectLoader = nullptr;
		class ParticleWorldSystem* _particleWorldSystem = nullptr;

	protected:
		class FileSystem* _fileSystem = nullptr;
		class UIManager* _uiManager = nullptr;
		class ResourceSystem* _resourceSystem = nullptr;

		std::thread _physicsThread;		
		std::vector<IGameExtension*> _gameExtensions;

	public:
		/** @brief Returns whether the environment main loop is running. */
		virtual bool IsRunning() = 0;

		/** @brief Starts and runs the environment main loop. */
		virtual void Run() = 0;

		/** @brief Requests orderly shutdown (typically on window close). */
		virtual void OnRecieveQuitMessage() = 0;

		/** @brief Returns the current output aspect ratio. */
		virtual float GetAspectRatio() = 0;

		/**
		 * @brief Handles window resize notifications.
		 * @param window Window that changed size.
		 * @param width New width in pixels.
		 * @param height New height in pixels.
		 */
		virtual void OnResizeWindow(Window* window, uint32_t width, uint32_t height) = 0;

		/**
		 * @brief Returns the current screen size.
		 * @param width Output width in pixels.
		 * @param height Output height in pixels.
		 */
		virtual void GetScreenSize(uint32_t& width, uint32_t& height) const = 0;

		/** @brief Returns X-axis screen scale factor used by UI/layout code. */
		virtual float GetScreenScaleX() const = 0;

		/** @brief Returns Y-axis screen scale factor used by UI/layout code. */
		virtual float GetScreenScaleY() const = 0;

		/** @brief Returns current screen width in pixels. */
		virtual int32_t GetScreenWidth() const = 0;

		/** @brief Returns current screen height in pixels. */
		virtual int32_t GetScreenHeight() const = 0;

		/** @brief Returns whether the main window currently has focus. */
		virtual bool GetHasFocus() const = 0;

		/** @brief Updates window focus state. */
		virtual void SetHasFocus(bool hasFocus) = 0;

		/** @brief Enables/disables editor mode behavior in the runtime. */
		virtual void SetEditorMode(bool editorMode) = 0;

		/** @brief Returns whether the runtime is currently in editor mode. */
		virtual bool IsEditorMode() const = 0;

		/**
		 * @brief Adds a game extension instance to the active extension list.
		 * @param extension Game extension to register.
		 */
		void AddGameExtension(IGameExtension* extension);
		/**
		 * @brief Removes a game extension instance from the active extension list.
		 * @param extension Game extension to unregister.
		 */
		void RemoveGameExtension(IGameExtension* extension);

	protected:
		/** @brief Implementation-specific destruction hook invoked by DestroyEnvironment. */
		virtual void Destroy() = 0;
	};

	/** @brief Global pointer to the active engine environment. */
	HEX_API extern IEnvironment* g_pEnv;

	/** @brief Destroys the active global environment instance (`g_pEnv`). */
	void HEX_API DestroyEnvironment();

#ifdef _DEBUG
	#define LOG_DEBUG(text, ...) if(HexEngine::g_pEnv && HexEngine::g_pEnv->_logFile){HexEngine::g_pEnv->_logFile->WriteLine(HexEngine::LogLevel::Debug, __FILE__, __FUNCTION__, __LINE__, text, __VA_ARGS__); }
	#define LOG_INFO(text, ...) if(HexEngine::g_pEnv && HexEngine::g_pEnv->_logFile){HexEngine::g_pEnv->_logFile->WriteLine(HexEngine::LogLevel::Info, __FILE__, __FUNCTION__, __LINE__, text, __VA_ARGS__); }
	#define LOG_WARN(text, ...) if(HexEngine::g_pEnv && HexEngine::g_pEnv->_logFile){HexEngine::g_pEnv->_logFile->WriteLine(HexEngine::LogLevel::Warn, __FILE__, __FUNCTION__, __LINE__, text, __VA_ARGS__); }
	#define LOG_CRIT(text, ...) if(HexEngine::g_pEnv && HexEngine::g_pEnv->_logFile){HexEngine::g_pEnv->_logFile->WriteLine(HexEngine::LogLevel::Crit, __FILE__, __FUNCTION__, __LINE__, text, __VA_ARGS__); }
#else
	#define LOG_DEBUG(text, ...) if(HexEngine::g_pEnv && HexEngine::g_pEnv->_logFile){HexEngine::g_pEnv->_logFile->WriteLine(HexEngine::LogLevel::Debug, text, __VA_ARGS__); }
	#define LOG_INFO(text, ...) if(HexEngine::g_pEnv && HexEngine::g_pEnv->_logFile){HexEngine::g_pEnv->_logFile->WriteLine(HexEngine::LogLevel::Info, text, __VA_ARGS__); }
	#define LOG_WARN(text, ...) if(HexEngine::g_pEnv && HexEngine::g_pEnv->_logFile){HexEngine::g_pEnv->_logFile->WriteLine(HexEngine::LogLevel::Warn, text, __VA_ARGS__); }
	#define LOG_CRIT(text, ...)if(HexEngine::g_pEnv && HexEngine::g_pEnv->_logFile){HexEngine:: g_pEnv->_logFile->WriteLine(HexEngine::LogLevel::Crit, text, __VA_ARGS__); }
#endif
}
