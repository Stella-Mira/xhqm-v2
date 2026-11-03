// astral 并发读写压测（二进制索引路径）
//   gen <file> <count>   流式生成 count 个节点的二进制库（索引+数据分离，与 kernel::save_binary 同格式）
//   run <file>           load_index 只载索引，20 线程 × 10000 次，读写比 7:3 / 3:7
// 读 = get(id) 点查；写 = update(form,id,value) 追加数据
// 节点 uuid 由 make_uuid(i) 确定性生成，测试端无需存储 1e8 个 uuid。
#include "../network/astral/kernel.h"
#include "../network/astral/mmap_index.h"
#include <thread>
#include <atomic>
#include <random>
#include <chrono>
#include <vector>
#include <fstream>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
#endif

using namespace xhqm;
using namespace xhqm::astral;

static const int THREADS = 20;
static const int OPS_PER_THREAD = 10000;

static kernel g_mem;
static std::atomic<size_t> g_read{ 0 }, g_write{ 0 }, g_read_fail{ 0 };

static double working_set_gb()
{
#ifdef _WIN32
	PROCESS_MEMORY_COUNTERS pmc{};
	if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
		return (double)pmc.WorkingSetSize / 1024.0 / 1024.0 / 1024.0;
#endif
	return 0.0;
}

// 确定性 uuid（低 6 字节放 i 的低 48 位，字节 6=版本4，字节 8=变体），i<2^48 时唯一
static uuid make_uuid(size_t i)
{
	uuid u{};
	for (int k = 0; k < 6; ++k) u(k) = (xhqm::byte)(i >> (8 * k));
	u(7) = (xhqm::byte)(i >> 48);
	u(6) = (xhqm::byte)((u(6) & 0x0F) | 0x40);   // version 4
	u(8) = (xhqm::byte)((u(8) & 0x3F) | 0x80);   // RFC4122 variant
	return u;
}

// ---- 流式生成二进制库（与 kernel::save_binary 布局一致，但不把全量节点载入内存）----
static void write_cbor_text(std::ofstream& out, const char* s)
{
	size_t n = std::strlen(s);
	out.put((char)(0x60 + n));   // text string, len<24
	out.write(s, (std::streamsize)n);
}

static void write_cbor_array_header(std::ofstream& out, size_t count)
{
	out.put((char)0x9A);         // array, 4-byte length
	for (int k = 3; k >= 0; --k) out.put((char)(count >> (8 * k)));
}

static void generate_binary(const char* path, size_t count)
{
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if (!out.is_open()) { fprintf(stderr, "[gen] cannot open %s\n", path); return; }

	// 头：ASTRAL + ver + flags + isz（占位）
	out.write("ASTRAL", 6);
	char ver = 1, flags = 0;
	out.write(&ver, 1);
	out.write(&flags, 1);
	uint64_t isz = 0;
	out.write((const char*)&isz, 8);

	// 索引：{version:1, nodes:[{uuid,info,links,off,size}...]}（键按字母序：nodes 先于 version）
	out.put((char)0xA2);                       // map(2)
	write_cbor_text(out, "nodes");
	write_cbor_array_header(out, count);

	auto t0 = std::chrono::steady_clock::now();
	for (size_t i = 0; i < count; ++i)
	{
		uuid u = make_uuid(i);
		json entry;
		entry["uuid"] = xhqm::cipher::uuid::to_text(u);
		entry["info"] = "";
		entry["links"] = json::array();
		entry["off"] = (uint64_t)i;
		entry["size"] = (uint64_t)1;
		std::vector<uint8_t> cb = json::to_cbor(entry);
		out.write((const char*)cb.data(), (std::streamsize)cb.size());

		if ((i + 1) % 1000000 == 0)
		{
			auto t1 = std::chrono::steady_clock::now();
			double sec = std::chrono::duration<double>(t1 - t0).count();
			fprintf(stderr, "[gen] %zu/%zu  %.1fs  %.0f/s\n", i + 1, count, sec, (i + 1) / sec);
		}
	}

	write_cbor_text(out, "version");
	out.put((char)0x01);

	isz = (uint64_t)out.tellp() - 16;          // 索引段字节数
	out.seekp(8);
	out.write((const char*)&isz, 8);

	// 数据段：每节点 1 字节空数组 CBOR(0x80)
	out.seekp(0, std::ios::end);
	std::vector<char> blob(count, (char)0x80);
	out.write(blob.data(), (std::streamsize)blob.size());
	out.close();

	fprintf(stderr, "[gen] done: %s  count=%zu  size=%.2fGB\n", path, count, (double)(16 + isz + count) / 1024 / 1024 / 1024);
}

// ---- 并发读写 ----
static void worker(size_t n, int read_r, int write_r)
{
	uint64_t seed = (uint64_t)std::random_device{}() ^ (uint64_t)(uintptr_t)&seed;
	std::mt19937_64 rng(seed);
	for (int i = 0; i < OPS_PER_THREAD; ++i)
	{
		uuid id = make_uuid(rng() % n);
		if ((int)(rng() % (read_r + write_r)) < read_r)
		{
			const node* nd = g_mem.get(id);
			if (nd) g_read.fetch_add(1, std::memory_order_relaxed);
			else    g_read_fail.fetch_add(1, std::memory_order_relaxed);
		}
		else
		{
			g_mem.update_data("t", id, json(1));
			g_write.fetch_add(1, std::memory_order_relaxed);
		}
	}
}

static void run_ratio(size_t n, int read_r, int write_r)
{
	g_read = 0; g_write = 0; g_read_fail = 0;
	auto t0 = std::chrono::steady_clock::now();
	std::vector<std::thread> th;
	th.reserve(THREADS);
	for (int i = 0; i < THREADS; ++i) th.emplace_back(worker, n, read_r, write_r);
	for (auto& t : th) t.join();
	auto t1 = std::chrono::steady_clock::now();
	double sec = std::chrono::duration<double>(t1 - t0).count();
	size_t total = g_read + g_write;
	fprintf(stderr, "[%d:%d] read=%zu write=%zu fail=%zu total=%zu time=%.2fs  %.0f ops/s\n",
		read_r, write_r,
		(size_t)g_read, (size_t)g_write, (size_t)g_read_fail, total, sec, total / sec);
}

static void run_binary_test(const char* path)
{
	auto t0 = std::chrono::steady_clock::now();
	if (!g_mem.load_index(path))
	{
		fprintf(stderr, "[run] load_index failed: %s\n", path);
		return;
	}
	auto t1 = std::chrono::steady_clock::now();
	double load_sec = std::chrono::duration<double>(t1 - t0).count();
	size_t n = g_mem.count();
	fprintf(stderr, "[run] loaded %zu nodes in %.2fs  rss=%.2fGB\n", n, load_sec, working_set_gb());

	run_ratio(n, 7, 3);
	run_ratio(n, 3, 7);

	fprintf(stderr, "[done] count=%zu fail=%zu rss=%.2fGB\n",
		(size_t)g_mem.count(), (size_t)g_read_fail, working_set_gb());
}

int main(int argc, char** argv)
{
	if (argc >= 4 && std::strcmp(argv[1], "gen") == 0)
	{
		generate_binary(argv[2], strtoull(argv[3], nullptr, 10));
		return 0;
	}
	if (argc >= 3 && std::strcmp(argv[1], "run") == 0)
	{
		run_binary_test(argv[2]);
		return (g_read_fail == 0) ? 0 : 1;
	}
	fprintf(stderr, "usage: astral_stress_test gen <file> <count>\n");
	fprintf(stderr, "       astral_stress_test run <file>\n");
	return 2;
}
