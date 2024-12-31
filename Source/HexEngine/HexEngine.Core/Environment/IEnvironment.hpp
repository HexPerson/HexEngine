

#pragma once

#include "../Required.hpp"
#include "../Graphics/MeshLoader.hpp"

namespace HexEngine
{
	#define DEV_RESOLUTION_X 3840
	#define DEV_RESOLUTION_Y 2081 //2160 2081 is when in windowed editor mode

	class IGameExtension;

	class IEnvironment
	{
	public:

		static void DestroyEnvironment(IEnvironment* environment);

		bool IsPhysicsSystemEnabled()
		{
			return _physicsSystem != nullptr;
		}

		class FileSystem* _fileSystem = nullptr;
		class IGraphicsDevice* _graphicsDevice = nullptr;
		class TimeManager* _timeManager = nullptr;
		class LogFile* _logFile = nullptr;
		class IModelImporter* _modelImporter = nullptr;
		class ResourceSystem* _resourceSystem = nullptr;
		class ShaderSystem* _shaderLoader = nullptr;
		class SceneManager* _sceneManager = nullptr;
		class SceneRenderer* _sceneRenderer = nullptr;
		class MeshPrimitives* _meshPrimitives = nullptr;		
		class InputSystem* _inputSystem = nullptr;
		class Window* _window = nullptr;
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
		class UIManager* _uiManager = nullptr;
		class AssetPackageManager* _assetPackageManager = nullptr;
		class IconService* _iconService = nullptr;
		//class IScriptEngine* _scriptEngine = nullptr; // disabled currently
		class IStreamlineProvider* _streamlineProvider = nullptr;
		class IDenoiserProvider* _denoiserProvider = nullptr;
		MeshLoader* _meshLoader = nullptr;

		std::thread _physicsThread;		
		std::vector<IGameExtension*> _gameExtensions;

	public:
		virtual bool IsRunning() = 0;

		virtual void Run() = 0;

		virtual void OnRecieveQuitMessage() = 0;

		virtual float GetAspectRatio() = 0;

		virtual void OnResizeWindow(uint32_t width, uint32_t height, HWND handle = 0) = 0;

		virtual void GetScreenSize(uint32_t& width, uint32_t& height) const = 0;

		virtual float GetScreenScaleX() const = 0;

		virtual float GetScreenScaleY() const = 0;

		virtual int32_t GetScreenWidth() const = 0;

		virtual int32_t GetScreenHeight() const = 0;

		virtual bool GetHasFocus() const = 0;

		virtual void SetHasFocus(bool hasFocus) = 0;

		virtual void SetEditorMode(bool editorMode) = 0;

		virtual bool IsEditorMode() const = 0;

		void AddGameExtension(IGameExtension* extension);
		void RemoveGameExtension(IGameExtension* extension);

	protected:
		virtual void Destroy() = 0;
	};

	extern IEnvironment* g_pEnv;

	/// <summary>
	/// Helper function to destroy the game environment
	/// </summary>
	void DestroyEnvironment();

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