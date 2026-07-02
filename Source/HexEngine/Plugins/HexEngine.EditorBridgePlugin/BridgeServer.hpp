
#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <thread>

namespace HexEngine
{
namespace EditorBridge
{
	// A tiny local named-pipe server for the editor bridge. Single-connection at a
	// time (the MCP client does one request/response per connect), newline-framed.
	//
	// Threading + lifetime:
	//  - Runs its accept/serve loop on ONE owned background thread (never detached).
	//  - Start() spins the thread; Stop() signals a stop event, cancels any pending
	//    I/O, and joins. The destructor calls Stop(). All Win32 handles are RAII.
	//  - The line handler is invoked ON THE PIPE THREAD. It must not touch engine
	//    state directly; the plugin's handler marshals to the main thread.
	class BridgeServer
	{
	public:
		// handler: takes one request line (JSON, no newline) and returns one
		// response line (JSON, no newline). Invoked on the pipe thread.
		using LineHandler = std::function<std::string(const std::string&)>;

		BridgeServer() = default;
		~BridgeServer();

		BridgeServer(const BridgeServer&) = delete;
		BridgeServer& operator=(const BridgeServer&) = delete;

		// Start listening on `pipeName` (e.g. \\.\pipe\HexEngine.EditorBridge.<pid>).
		// Returns false if the thread could not be started. Safe to call once.
		bool Start(const std::string& pipeName, LineHandler handler);

		// Signal stop, cancel pending I/O, and join the thread. Idempotent.
		void Stop();

		bool IsRunning() const { return _running.load(); }
		const std::string& PipeName() const { return _pipeName; }

	private:
		void AcceptLoop();

		std::string        _pipeName;
		LineHandler        _handler;
		std::thread        _thread;
		std::atomic<bool>  _running{ false };
		void*              _stopEvent = nullptr; // HANDLE (manual-reset)
	};
}
}
