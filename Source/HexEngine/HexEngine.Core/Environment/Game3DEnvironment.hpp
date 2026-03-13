
#pragma once

#include "../HexEngine.hpp"
#include "../Graphics/Window.hpp"
#include "../Physics/IPhysicsSystem.hpp"
#include "IEnvironment.hpp"

namespace HexEngine
{
	enum GameOptions
	{
		GameOptions_None = 0,
		GameOptions_NoRenderer = HEX_BITSET(0),
	};
	DEFINE_ENUM_FLAG_OPERATORS(GameOptions);

	struct Game3DOptions
	{
		Window* window = nullptr;
		std::wstring applicationName;
		bool createIconService = false;
		GameOptions flags = GameOptions::GameOptions_None;
	};

	class Model;
	class AssetPackage;
	class IShader;

	class HEX_API Game3DEnvironment : public IEnvironment
	{
	public:
		Game3DEnvironment();

		static Game3DEnvironment* Create(const Game3DOptions& options);

		virtual bool IsRunning() override;

		virtual void Run() override;

		virtual void OnRecieveQuitMessage() override;

		virtual float GetAspectRatio() override;

		virtual void OnResizeWindow(Window* window, uint32_t width, uint32_t height) override;

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
		int32_t _windowWidth = 0;
		int32_t _windowHeight = 0;
		HWND _windowHandle = 0;
		bool _isPhysicsThreadActive = false;
		bool _hasFocus = true;
		bool _inEditorMode = false;
		std::shared_ptr<AssetPackage> _standardAssets = nullptr;
		std::shared_ptr<IShader> _hdrPresentShader;
	};
}
