// Minimal, dependency-free unit-test harness for HexEngine hardening work.
// Compiles the pure sources under test directly (no heavy Core link). PR6 will
// formalize this project (CI integration, more coverage, static analysis).

#include <cstdio>
#include <string>
#include <vector>
#include <cstdint>

#include <thread>
#include <atomic>

#include "../HexEngine.Core/Plugin/PluginManifest.hpp"
#include "../HexEngine.Core/Utility/Sha256.hpp"
#include "../HexEngine.Core/FileSystem/BinaryReader.hpp"
#include "../HexEngine.Core/Utility/BlockingQueue.hpp"

using namespace HexEngine;

static int g_total = 0;
static int g_fail = 0;

#define CHECK(cond) do { ++g_total; if(!(cond)) { ++g_fail; std::printf("  FAIL [%s:%d] %s\n", __FILE__, __LINE__, #cond); } } while(0)

static void TestManifestParse()
{
	PluginManifest m; std::string err;

	CHECK(ParsePluginManifest(R"({"plugins":[{"name":"P","module":"P.dll"}]})", m, err));
	CHECK(m.entries.size() == 1);
	CHECK(m.entries.size() == 1 && m.entries[0].module == "P.dll");
	CHECK(m.entries.size() == 1 && m.entries[0].enabled == true);

	CHECK(!ParsePluginManifest("{ not json ", m, err));                    // malformed JSON
	CHECK(!ParsePluginManifest(R"({"nope":1})", m, err));                  // missing 'plugins'
	CHECK(!ParsePluginManifest(R"({"plugins":42})", m, err));              // 'plugins' not an array
	CHECK(!ParsePluginManifest(R"({"plugins":[{"name":"x"}]})", m, err));  // entry missing 'module'
}

static void TestEvaluate()
{
	PluginManifest m; std::string err;
	ParsePluginManifest(R"({"plugins":[
		{"name":"Good","module":"Good.dll","enabled":true,"sha256":"ABCD"},
		{"name":"Off","module":"Off.dll","enabled":false},
		{"name":"NoHash","module":"NoHash.dll","enabled":true}
	]})", m, err);

	const std::string good = "abcd"; // matches "ABCD" case-insensitively
	const std::string bad  = "0000";

	// Developer: permissive.
	CHECK(EvaluatePluginLoad(m, PluginLoadPolicy::Developer, "Unlisted.dll", nullptr) == PluginLoadDecision::Allow);
	CHECK(EvaluatePluginLoad(m, PluginLoadPolicy::Developer, "Off.dll",      nullptr) == PluginLoadDecision::Reject_Disabled);
	CHECK(EvaluatePluginLoad(m, PluginLoadPolicy::Developer, "Good.dll",     nullptr) == PluginLoadDecision::Allow);            // skips uncomputed hash
	CHECK(EvaluatePluginLoad(m, PluginLoadPolicy::Developer, "Good.dll",     &bad)    == PluginLoadDecision::Reject_HashMismatch); // but rejects a real mismatch
	CHECK(EvaluatePluginLoad(m, PluginLoadPolicy::Developer, "Good.dll",     &good)   == PluginLoadDecision::Allow);

	// Production: fail closed.
	CHECK(EvaluatePluginLoad(m, PluginLoadPolicy::Production, "Unlisted.dll", &good)   == PluginLoadDecision::Reject_NotInManifest);
	CHECK(EvaluatePluginLoad(m, PluginLoadPolicy::Production, "Off.dll",      &good)   == PluginLoadDecision::Reject_Disabled);
	CHECK(EvaluatePluginLoad(m, PluginLoadPolicy::Production, "Good.dll",     nullptr) == PluginLoadDecision::Reject_HashUnverified);
	CHECK(EvaluatePluginLoad(m, PluginLoadPolicy::Production, "Good.dll",     &bad)    == PluginLoadDecision::Reject_HashMismatch);
	CHECK(EvaluatePluginLoad(m, PluginLoadPolicy::Production, "Good.dll",     &good)   == PluginLoadDecision::Allow);
	CHECK(EvaluatePluginLoad(m, PluginLoadPolicy::Production, "NoHash.dll",   nullptr) == PluginLoadDecision::Allow); // listed+enabled, no hash pinned

	// Case-insensitive module matching.
	CHECK(EvaluatePluginLoad(m, PluginLoadPolicy::Production, "good.DLL",     &good)   == PluginLoadDecision::Allow);
}

static void TestSha256()
{
	// Known NIST vectors.
	CHECK(Sha256Hex("abc", 3) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	CHECK(Sha256Hex("", 0)    == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

// --- BinaryReader ---------------------------------------------------------

// Little-endian u32 as 4 bytes, matching how DiskFile::WriteString / the loaders
// emit length prefixes on this (x64, little-endian) target.
static void PushU32(std::vector<uint8_t>& v, uint32_t x)
{
	v.push_back((uint8_t)(x & 0xFF));
	v.push_back((uint8_t)((x >> 8) & 0xFF));
	v.push_back((uint8_t)((x >> 16) & 0xFF));
	v.push_back((uint8_t)((x >> 24) & 0xFF));
}

static void PushStr(std::vector<uint8_t>& v, const std::string& s)
{
	PushU32(v, (uint32_t)s.size());
	v.insert(v.end(), s.begin(), s.end());
}

static void TestBinaryReaderBasics()
{
	// POD reads advance the cursor and stop cleanly at EOF.
	std::vector<uint8_t> buf;
	PushU32(buf, 0xDEADBEEF);
	buf.push_back(0x2A); // one trailing byte

	BinaryReader r(buf);
	CHECK(r.Size() == 5);
	CHECK(r.Remaining() == 5);

	uint32_t u = 0;
	CHECK(r.Read(u) && u == 0xDEADBEEFu);
	CHECK(r.Position() == 4 && r.Remaining() == 1);

	uint8_t b = 0;
	CHECK(r.Read(b) && b == 0x2A);
	CHECK(r.Eof() && r.Remaining() == 0 && r.Good());

	// Reading past the end fails, latches, and leaves the cursor at EOF.
	uint8_t after = 0;
	CHECK(!r.Read(after));
	CHECK(!r.Good());                    // sticky failure
	CHECK(!r.Read(u));                   // subsequent reads no-op

	// ReadOr returns the fallback (and latches) when short.
	BinaryReader r2(buf.data(), 2);      // only 2 bytes -> can't read a u32
	CHECK(r2.ReadOr<uint32_t>(0x1234) == 0x1234u);
	CHECK(!r2.Good());
}

static void TestBinaryReaderBytesAndSeek()
{
	std::vector<uint8_t> buf = { 1,2,3,4,5,6,7,8 };
	BinaryReader r(buf);

	uint8_t dst[4] = {};
	CHECK(r.ReadBytes(dst, 4) && dst[0] == 1 && dst[3] == 4);
	CHECK(r.Position() == 4);

	// Asking for more than remains fails without copying / advancing past end.
	uint8_t big[8] = {};
	CHECK(!r.ReadBytes(big, 8));
	CHECK(!r.Good());

	// Seek recovers position; out-of-range seek latches.
	BinaryReader r2(buf);
	CHECK(r2.Seek(6) && r2.Position() == 6 && r2.Remaining() == 2);
	CHECK(!r2.Seek(9));                  // past end
	CHECK(!r2.Good());

	// Skip respects bounds.
	BinaryReader r3(buf);
	CHECK(r3.Skip(8) && r3.Eof());
	BinaryReader r4(buf);
	CHECK(!r4.Skip(9));

	// ReadInPlace hands back a pointer into the span without copying.
	BinaryReader r5(buf);
	const uint8_t* p = r5.ReadInPlace(3);
	CHECK(p != nullptr && p[0] == 1 && p[2] == 3 && r5.Position() == 3);
	CHECK(r5.ReadInPlace(100) == nullptr);
}

static void TestBinaryReaderString()
{
	// Valid round-trip.
	{
		std::vector<uint8_t> buf;
		PushStr(buf, "material/brick.hmat");
		PushU32(buf, 7); // trailing extra field, proves the cursor stops after the string
		BinaryReader r(buf);
		std::string s;
		CHECK(r.ReadString(s) && s == "material/brick.hmat");
		uint32_t tail = 0;
		CHECK(r.Read(tail) && tail == 7);
	}

	// Zero-length string is valid and consumes only the prefix.
	{
		std::vector<uint8_t> buf;
		PushStr(buf, "");
		BinaryReader r(buf);
		std::string s = "dirty";
		CHECK(r.ReadString(s) && s.empty() && r.Eof());
	}

	// Length exceeding the bytes actually present -> fail, no over-read/alloc.
	{
		std::vector<uint8_t> buf;
		PushU32(buf, 0xFFFFFFFFu);       // claims ~4 GB
		buf.push_back('h'); buf.push_back('i');
		BinaryReader r(buf);
		std::string s;
		CHECK(!r.ReadString(s));
		CHECK(s.empty() && !r.Good());
	}

	// Length within the span but above an explicit caller cap -> fail.
	{
		std::vector<uint8_t> buf;
		PushStr(buf, "abcdefghij");       // 10 bytes
		BinaryReader r(buf);
		std::string s;
		CHECK(!r.ReadString(s, 4));        // cap of 4 rejects a 10-byte string
		CHECK(!r.Good());
	}

	// Truncated prefix (fewer than 4 bytes) -> fail, not a read of garbage length.
	{
		std::vector<uint8_t> buf = { 0x05, 0x00 };
		BinaryReader r(buf);
		std::string s;
		CHECK(!r.ReadString(s));
		CHECK(!r.Good());
	}
}

static void TestBinaryReaderCount()
{
	// Valid count that fits the remaining bytes.
	{
		std::vector<uint8_t> buf;
		PushU32(buf, 3);                  // 3 elements
		for (int i = 0; i < 3 * 8; ++i) buf.push_back((uint8_t)i); // 3 * 8-byte elems
		BinaryReader r(buf);
		uint32_t n = 0;
		CHECK(r.ReadCount(n, 8) && n == 3);
		CHECK(r.Remaining() == 24);
	}

	// Bogus huge count in a tiny buffer -> fail before any allocation.
	{
		std::vector<uint8_t> buf;
		PushU32(buf, 0x10000000u);        // 268M elements
		buf.push_back(0); buf.push_back(0); // only 2 payload bytes
		BinaryReader r(buf);
		uint32_t n = 123;
		CHECK(!r.ReadCount(n, 16));        // 268M * 16 bytes >> remaining
		CHECK(n == 0 && !r.Good());
	}

	// count * elementSize must not wrap: a count just over the remaining budget fails.
	{
		std::vector<uint8_t> buf;
		PushU32(buf, 5);
		for (int i = 0; i < 4; ++i) buf.push_back(0); // only room for 4 one-byte elems
		BinaryReader r(buf);
		uint32_t n = 0;
		CHECK(!r.ReadCount(n, 1));         // claims 5, only 4 remain
		CHECK(!r.Good());
	}

	// Count above an explicit max cap fails even if bytes would fit.
	{
		std::vector<uint8_t> buf;
		PushU32(buf, 10);
		for (int i = 0; i < 10; ++i) buf.push_back(0);
		BinaryReader r(buf);
		uint32_t n = 0;
		CHECK(!r.ReadCount(n, 1, /*maxCount*/ 4));
		CHECK(!r.Good());
	}

	// Zero elementSize is a programming error and is rejected.
	{
		std::vector<uint8_t> buf;
		PushU32(buf, 1);
		BinaryReader r(buf);
		uint32_t n = 0;
		CHECK(!r.ReadCount(n, 0));
	}
}

// Mirrors the AssetFile V1 block walk: a stream of {fixed record header, then
// `size` inline payload bytes}. The hardened walk uses ReadInPlace(headerSize) +
// Skip(size); a record whose size runs past the buffer must be rejected instead
// of walking the cursor out of bounds (the original OOB heap read).
static bool WalkPackedBlocks(const std::vector<uint8_t>& buf, uint32_t numBlocks, size_t headerSize, int& outWalked)
{
	outWalked = 0;
	BinaryReader r(buf);
	for (uint32_t i = 0; i < numBlocks; ++i)
	{
		const uint8_t* hdr = r.ReadInPlace(headerSize);
		if (!hdr)
			return false;                 // header runs past end
		uint32_t size = 0;
		std::memcpy(&size, hdr, sizeof(size)); // first field of the record is the payload size
		if (!r.Skip(size))
			return false;                 // payload runs past end
		++outWalked;
	}
	return true;
}

static void TestBinaryReaderPackedWalk()
{
	const size_t headerSize = 8; // {uint32 size; uint32 pad;}

	// Two well-formed blocks: sizes 3 and 2.
	{
		std::vector<uint8_t> buf;
		PushU32(buf, 3); PushU32(buf, 0); buf.insert(buf.end(), { 'a','b','c' });
		PushU32(buf, 2); PushU32(buf, 0); buf.insert(buf.end(), { 'd','e' });
		int walked = 0;
		CHECK(WalkPackedBlocks(buf, 2, headerSize, walked));
		CHECK(walked == 2);
	}

	// Malicious: header claims a 4 GB payload in a tiny buffer -> rejected, and
	// the walk stops without ever dereferencing out of bounds.
	{
		std::vector<uint8_t> buf;
		PushU32(buf, 0xFFFFFFFFu); PushU32(buf, 0); buf.insert(buf.end(), { 'x','y' });
		int walked = 0;
		CHECK(!WalkPackedBlocks(buf, 1, headerSize, walked));
		CHECK(walked == 0);
	}

	// Count claims more blocks than the buffer holds -> second header read fails.
	{
		std::vector<uint8_t> buf;
		PushU32(buf, 1); PushU32(buf, 0); buf.insert(buf.end(), { 'z' });
		int walked = 0;
		CHECK(!WalkPackedBlocks(buf, 5, headerSize, walked)); // asks for 5, only 1 present
		CHECK(walked == 1);
	}
}

// --- BlockingQueue --------------------------------------------------------

static void TestBlockingQueueBasics()
{
	BlockingQueue<int> q;
	CHECK(q.Empty() && q.Size() == 0 && !q.IsShutdown());

	q.Push(1); q.Push(2); q.Push(3);
	CHECK(q.Size() == 3);

	int v = 0;
	// FIFO order.
	CHECK(q.WaitPop(v) && v == 1);
	CHECK(q.WaitPop(v) && v == 2);
	CHECK(q.TryPop(v) && v == 3);
	CHECK(!q.TryPop(v));            // now empty
	CHECK(q.Empty());

	// Shutdown makes WaitPop return false and refuses new work.
	q.Shutdown();
	CHECK(q.IsShutdown());
	q.Push(99);                     // ignored while shut down
	CHECK(q.Empty());
	int untouched = 42;
	CHECK(!q.WaitPop(untouched) && untouched == 42);
}

static void TestBlockingQueueConcurrent()
{
	// Multi-producer / multi-consumer: every produced item is consumed exactly
	// once, and Shutdown() reliably wakes idle consumers so nothing hangs.
	BlockingQueue<int> q;
	const int producers = 4;
	const int perProducer = 1000;
	const int total = producers * perProducer;

	std::atomic<int> consumedCount{ 0 };
	std::atomic<long long> consumedSum{ 0 };

	std::vector<std::thread> consumers;
	for (int c = 0; c < 3; ++c)
	{
		consumers.emplace_back([&]
		{
			int v = 0;
			while (q.WaitPop(v))
			{
				consumedCount.fetch_add(1, std::memory_order_relaxed);
				consumedSum.fetch_add(v, std::memory_order_relaxed);
			}
		});
	}

	long long expectedSum = 0;
	std::vector<std::thread> prods;
	for (int p = 0; p < producers; ++p)
	{
		for (int i = 0; i < perProducer; ++i)
			expectedSum += (p * perProducer + i);

		prods.emplace_back([&, p]
		{
			for (int i = 0; i < perProducer; ++i)
				q.Push(p * perProducer + i);
		});
	}

	for (auto& t : prods) t.join();

	// Wait for consumers to drain everything before shutting down (Shutdown
	// drops any still-queued items, so we must let them all be consumed first).
	while (consumedCount.load(std::memory_order_relaxed) < total || !q.Empty())
		std::this_thread::yield();

	q.Shutdown();
	for (auto& t : consumers) t.join();  // must not hang - Shutdown woke them

	CHECK(consumedCount.load() == total);
	CHECK(consumedSum.load() == expectedSum);
}

int main()
{
	std::printf("HexEngine.Tests\n");
	TestManifestParse();
	TestEvaluate();
	TestSha256();
	TestBinaryReaderBasics();
	TestBinaryReaderBytesAndSeek();
	TestBinaryReaderString();
	TestBinaryReaderCount();
	TestBinaryReaderPackedWalk();
	TestBlockingQueueBasics();
	TestBlockingQueueConcurrent();
	std::printf("\n%d/%d checks passed.\n", g_total - g_fail, g_total);
	std::printf(g_fail == 0 ? "RESULT: OK\n" : "RESULT: FAILED\n");
	return g_fail == 0 ? 0 : 1;
}
