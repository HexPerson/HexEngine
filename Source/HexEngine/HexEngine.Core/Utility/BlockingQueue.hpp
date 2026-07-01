
#pragma once

#include <condition_variable>
#include <mutex>
#include <deque>
#include <utility>
#include <cstddef>

namespace HexEngine
{
	// -------------------------------------------------------------------------
	// BlockingQueue<T> - a small multi-producer / multi-consumer FIFO with a
	// condition-variable so consumers BLOCK until work arrives instead of
	// polling with sleeps. All locking is RAII (std::unique_lock / lock_guard)
	// so an exception in a producer/consumer can never leave the mutex held.
	//
	// Shutdown semantics: Shutdown() wakes every blocked consumer. Once shutdown
	// has been requested, WaitPop() returns false immediately (even if items
	// remain), so worker threads unblock promptly at teardown and stop pulling
	// new work. Callers that must finish everything already queued should drain
	// with TryPop() before calling Shutdown().
	//
	// Header-only and dependency-free so it can be unit-tested in isolation and
	// reused by any producer/consumer subsystem (ResourceSystem's async loader
	// is the first user - PR4).
	// -------------------------------------------------------------------------
	template<typename T>
	class BlockingQueue
	{
	public:
		BlockingQueue() = default;
		BlockingQueue(const BlockingQueue&) = delete;
		BlockingQueue& operator=(const BlockingQueue&) = delete;

		// Enqueue an item and wake one waiting consumer. No-op once shut down
		// (a shutting-down queue accepts no new work).
		void Push(T item)
		{
			{
				std::lock_guard<std::mutex> lock(_mutex);
				if (_shutdown)
					return;
				_queue.push_back(std::move(item));
			}
			_cv.notify_one();
		}

		// Block until an item is available or the queue is shut down. Returns
		// true and moves the front item into `out`; returns false if the queue
		// has been shut down (in which case `out` is left unchanged).
		bool WaitPop(T& out)
		{
			std::unique_lock<std::mutex> lock(_mutex);
			_cv.wait(lock, [this] { return _shutdown || !_queue.empty(); });
			if (_shutdown)
				return false;
			out = std::move(_queue.front());
			_queue.pop_front();
			return true;
		}

		// Non-blocking pop. Returns false if the queue is currently empty.
		bool TryPop(T& out)
		{
			std::lock_guard<std::mutex> lock(_mutex);
			if (_queue.empty())
				return false;
			out = std::move(_queue.front());
			_queue.pop_front();
			return true;
		}

		// Request shutdown: wake every blocked consumer so their WaitPop()
		// returns false and their loops exit. Idempotent.
		void Shutdown()
		{
			{
				std::lock_guard<std::mutex> lock(_mutex);
				_shutdown = true;
			}
			_cv.notify_all();
		}

		bool IsShutdown() const
		{
			std::lock_guard<std::mutex> lock(_mutex);
			return _shutdown;
		}

		size_t Size() const
		{
			std::lock_guard<std::mutex> lock(_mutex);
			return _queue.size();
		}

		bool Empty() const
		{
			std::lock_guard<std::mutex> lock(_mutex);
			return _queue.empty();
		}

	private:
		mutable std::mutex      _mutex;
		std::condition_variable _cv;
		std::deque<T>           _queue;
		bool                    _shutdown = false;
	};
}
