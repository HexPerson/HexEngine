
#pragma once

#include "../HexEngine.hpp"
#include "../Graphics/Window.hpp"
#include "../Physics/IPhysicsSystem.hpp"
#include "IEnvironment.hpp"

namespace HexEngine
{
	struct Game3DOptions
	{
		Window* window = nullptr;
		HWND windowHandle = 0;
		uint32_t windowWidth = 0;
		uint32_t windowHeight = 0;
		std::wstring applicationName;
		bool createIconService = false;
	};

	class Model;
	class AssetPackage;

	class Game3DEnvironment : public IEnvironment
	{
	public:
		Game3DEnvironment();

		static Game3DEnvironment* Create(const Game3DOptions& options);

		virtual bool IsRunning() override;

		virtual void Run() override;

		virtual void OnRecieveQuitMessage() override;

		virtual float GetAspectRatio() override;

		virtual void OnResizeWindow(uint32_t width, uint32_t height, HWND handle = 0) override;

		virtual void GetScreenSize(uint32_t& width, uint32_t& height) const override;

		virtual float GetScreenScaleX() const override;

		virtual float GetScreenScaleY() const override;

		virtual int32_t GetScreenWidth() const override;

		virtual int32_t GetScreenHeight() const override;

		virtual bool GetHasFocus() const override;

		virtual void SetHasFocus(bool hasFocus) override;

		virtual void SetEditorMode(bool editorMode) override;

		virtual bool IsEditorMode() const override;

		void FixedStep(float dt);

	private:
		virtual void Destroy() override;

		bool CreateGraphicsSystem(const Game3DOptions& options);

		void CreateLogFile(const Game3DOptions& options);

	private:
		bool _running;
		Window* _targetWindow;
		int32_t _windowWidth = 0;
		int32_t _windowHeight = 0;
		HWND _windowHandle = 0;
		bool _isPhysicsThreadActive = false;
		bool _hasFocus = true;
		bool _inEditorMode = false;
		AssetPackage* _standardAssets = nullptr;
	};
}
