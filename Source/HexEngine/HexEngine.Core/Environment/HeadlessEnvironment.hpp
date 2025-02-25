
#pragma once

#include "IEnvironment.hpp"
#include "IGameExtension.hpp"

namespace HexEngine
{
	struct HeadlessOptions
	{
		IGameExtension* gameExtension = nullptr;
	};

	class HEX_API HeadlessEnvironment : public IEnvironment
	{
	public:
		HeadlessEnvironment();

		static HeadlessEnvironment* Create(const HeadlessOptions& options);

		virtual bool IsRunning() override;

		virtual void Run() override;

		virtual void OnRecieveQuitMessage() override;

		virtual float GetAspectRatio() override;

		virtual void OnResizeWindow(Window* window, uint32_t width, uint32_t height) override;

		virtual void GetScreenSize(uint32_t& width, uint32_t& height) const override;

		virtual bool GetHasFocus() const override;

		virtual float GetScreenScaleX() const override;

		virtual float GetScreenScaleY() const override;

		virtual int32_t GetScreenWidth() const override;

		virtual int32_t GetScreenHeight() const override;

		virtual void SetHasFocus(bool hasFocus) override;

		virtual void SetEditorMode(bool editorMode) override {};

		virtual bool IsEditorMode() const override { return false; }

	private:
		virtual void Destroy() override;

		void CreateLogFile(const HeadlessOptions& options);

	private:
		bool _running;
		bool _isPhysicsThreadActive = false;
		bool _hasFocus = true;
	};
}
