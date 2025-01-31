
#pragma once

#include <HexEngine.Core/HexEngine.hpp>
#include <Ultralight/Ultralight.h>
#include "CreatorApp.hpp"

using namespace ultralight;

namespace HexCreator
{
	class Creator :
		public IGameExtension
	{
	public:
		Creator() {}
		virtual ~Creator() {}

		virtual void OnCreateGame() override;
		virtual void OnStopGame() {}
		virtual void OnLoadGameWorld() {}
		virtual void OnRegisterClasses() {}
		virtual void OnUpdate(float frameTime) {}
		virtual void OnFixedUpdate(float frameTime) {}
		virtual void OnShutdown() {}
		virtual void OnDebugGUI() {}
		virtual void OnGUI() override;
		virtual void OnDebugRender() {}
		virtual void OnResize(int32_t width, int32_t height) override {}
		virtual std::string GetGameName() const { return "HexEngine Creator"; }

		void CreateFileSystem(const fs::path& path);

	public:
		CreatorApp _app;
	};

	inline Creator* g_pCreator = nullptr;
}
