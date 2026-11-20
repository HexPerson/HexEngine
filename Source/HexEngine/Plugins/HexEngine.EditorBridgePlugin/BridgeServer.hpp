
#pragma once

#include <atomic>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace HexEngine
{
namespace EditorBridge
{
	// A tiny local named-pipe server for the editor bridge, newline-framed.
	//
	// Connections are PERSISTENT and CONCURRENT: each connected client gets its
	// own serving thread and may send any number of request lines; responses
	// are written back in order on the same connection. Up to kMaxClients
	// clients can be connected at once (e.g. the MCP server holding a long-lived
	// connection while an ad-hoc script makes one-shot calls).
	//
	// Threading + lifetime:
	//  - One accept thread plus one thread per connected client, all owned
	//    (never detached). Finished client threads are reaped by the accept loop.
	//  - Stop() signals a stop event that every blocking wait also watches,
	//    cancels pending I/O, and joins every thread. The destructor calls Stop().
	//  - The line handler is invoked ON A PIPE THREAD (possibly several at once).
	//    It must be thread-safe and must not touch engine state directly; the
	//    plugin's handler marshals to the main thread.
	class BridgeServer
	{
	public:
		// handler: takes one request line (JSON, no newline) and returns one
		// response line (JSON, no newline). Invoked on a pipe thread.
		using LineHandler = std::function<std::string(const std::string&)>;

		static constexpr unsigned kMaxClients = 8;

		BridgeServer() = default;
		~BridgeServer();

		BridgeServer(const BridgeServer&) = delete;
		BridgeServer& operator=(const BridgeServer&) = delete;

		// Start listening on `pipeName` (e.g. \\.\pipe\HexEngine.EditorBridge.<pid>).
		// Returns false if the thread could not be started. Safe to call once.
		bool Start(const std::string& pipeName, LineHandler handler);

		// Signal stop, cancel pending I/O, and join all threads. Idempotent.
		void Stop();

		bool IsRunning() const { return _running.load(); }
		const std::string& PipeName() const { return _pipeName; }

	private:
		struct Client
		{
			std::thread       thread;
			std::atomic<bool> done{ false };
		};

		void AcceptLoop();
		void ServeClient(void* pipe, Client* self);
		void ReapFinishedClients(bool all);

		std::string        _pipeName;
		LineHandler        _handler;
		std::thread        _thread;
		std::atomic<bool>  _running{ false };
		void*              _stopEvent = nullptr; // HANDLE (manual-reset)

		std::mutex                           _clientsMutex;
		std::list<std::unique_ptr<Client>>   _clients;
	};
}
}
