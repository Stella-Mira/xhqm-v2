// system 库测试：codesys / timesys / filesys / libmange / execsys / process / threads
#include "../software/system/codesys.h"
#include "../software/system/timesys.h"
#include "../software/system/filesys.h"
#include "../software/system/libmange.h"
#include "../software/system/execsys.h"
#include "../software/system/process/process_base.h"
#include "../software/system/threads/threads_pool.h"
#include "test.h"
#include <string>
#include <vector>
#include <atomic>
#include <filesystem>

using namespace xhqm;

TEST_CASE("test_codesys")
{
	// string2hexa / hexa2string
	CHECK(codesys::string2hexa("A") == "41");
	CHECK(codesys::hexa2string("41") == "A");
	const std::string s = "Hello,World!123";
	CHECK(codesys::hexa2string(codesys::string2hexa(s)) == s);

	// unicode 往返
	const std::string u8 = "中文测试";
	const std::wstring w = codesys::string_to_unicode(u8);
	CHECK(codesys::unicode_to_string(w) == u8);

	// split_string (char)：跳过空项
	auto parts = codesys::split_string("a,b,,c", ',');
	CHECK((parts.size() == 3 && parts[0] == "a" && parts[1] == "b" && parts[2] == "c"));

	// split_string (regex)
	std::vector<std::string> elems;
	codesys::split_string("a-b-c", "-", elems);
	CHECK((elems.size() == 3 && elems[0] == "a" && elems[2] == "c"));

	// utf8 / system 往返（ASCII 稳定）
	const std::string ascii = "plain_ascii_123";
	CHECK(codesys::system_to_utf8(codesys::utf8_to_system(ascii)) == ascii);
}

TEST_CASE("test_timesys")
{
	const auto n = timesys::ntime();
	const auto u = timesys::utime();
	const auto m = timesys::mtime();
	const auto s = timesys::stime();
	CHECK((n >= u && u >= m && m >= s));

	// sleep
	const auto before = timesys::mtime();
	timesys::msleep(20);
	const auto after = timesys::mtime();
	CHECK(after - before >= 20);

	// systemtime 字段
	const auto t = timesys::systemtime(timesys::mtime());
	CHECK(t.year >= 2000);
	CHECK((t.month >= 1 && t.month <= 12));
	CHECK((t.mday >= 1 && t.mday <= 31));
	CHECK((t.hour >= 0 && t.hour <= 23));

	// timer
	xhqm::timer tm;
	timesys::msleep(5);
	CHECK(tm.mtimes() >= 0);
	CHECK(tm.runtime() >= 5000000); // 至少 5ms = 500 万纳秒
}

TEST_CASE("test_filesys")
{
	const std::filesystem::path base(filesys::temp_directory());
	const std::string dir = (base / ("xhqm_sys_test_" + std::to_string(timesys::mtime()))).string();
	const std::string file = (std::filesystem::path(dir) / "test.txt").string();
	const std::string file2 = (std::filesystem::path(dir) / "test2.txt").string();
	const std::string renamed = (std::filesystem::path(dir) / "renamed.txt").string();

	// 清理残留
	filesys::delete_file(renamed);
	filesys::delete_file(file2);
	filesys::delete_file(file);
	filesys::delete_dir(dir);

	CHECK(filesys::create_dir(dir));
	CHECK(filesys::is_dir(dir));
	CHECK(filesys::is_exists(dir));

	CHECK(filesys::create_file(file));
	CHECK(filesys::is_file(file));

	auto files = filesys::file_list(dir);
	CHECK((files.size() == 1 && files[0] == "test.txt"));

	CHECK(filesys::copy_file(file, file2));
	CHECK(filesys::is_file(file2));

	CHECK(filesys::rename(file, renamed));
	CHECK(!filesys::is_exists(file));
	CHECK(filesys::is_exists(renamed));

	CHECK(filesys::delete_file(renamed));
	CHECK(filesys::delete_file(file2));
	CHECK(filesys::delete_dir(dir));
	CHECK(!filesys::is_dir(dir));

	CHECK(!filesys::temp_directory().empty());
	CHECK(!filesys::current_path().empty());
}

TEST_CASE("test_libmange")
{
	// loadlib 失败：加载不存在的库
	const libid bad = libmange::loadlib("__xhqm_nonexistent__.dll");
	CHECK(bad == nullptr);
	CHECK(!libmange::error().empty());

	// self 非空
	const libid self = libmange::self();
	CHECK(self != nullptr);

	// has_function 查不存在的符号
	CHECK(!libmange::has_function(self, "__xhqm_nonexistent_symbol__"));
}

TEST_CASE("test_execsys")
{
	// command 模式：echo
	const auto r = execsys::execute_cmd("echo hello_xhqm");
	CHECK(r.success());
	bool found = false;
	for (const auto& line : r.exec_out)
	{
		if (line.find("hello_xhqm") != std::string::npos) found = true;
	}
	CHECK(found);

	// 失败命令：退出码非 0
	const auto bad = execsys::execute_cmd("__xhqm_nonexistent_cmd__");
	CHECK(!bad.success());
}

TEST_CASE("test_process")
{
	xhqm::parallel::process proc;
	proc.command("echo hello_process");
	proc.exec();
	proc.wait();
	const auto r = proc.get_result();
	CHECK(r.success());
	bool found = false;
	for (const auto& line : r.exec_out)
	{
		if (line.find("hello_process") != std::string::npos) found = true;
	}
	CHECK(found);
}

TEST_CASE("test_threads")
{
	xhqm::parallel::threads pool(2, 4);

	// 带返回值任务（channel 0）
	auto h = pool.submit<int>(0, []() { return 42; });
	h.future_ptr()->wait();
	CHECK(h.future_ptr()->get() == 42);

	// 多通道任务
	std::atomic<int> sum{ 0 };
	auto h1 = pool.submit(0, [&sum]() { sum += 1; });
	auto h2 = pool.submit(1, [&sum]() { sum += 2; });
	h1.future_ptr()->wait();
	h2.future_ptr()->wait();
	CHECK(sum.load() == 3);

	// 批量任务
	std::atomic<int> total{ 0 };
	std::vector<xhqm::parallel::holder<int>> holders;
	for (int i = 0; i < 10; ++i)
	{
		holders.push_back(pool.submit<int>(i % 2, [&total, i]() { total += i; return i * i; }));
	}
	int sqsum = 0;
	for (auto& hh : holders)
	{
		hh.future_ptr()->wait();
		sqsum += hh.future_ptr()->get();
	}
	CHECK(total.load() == 45);      // 0+1+...+9
	CHECK(sqsum == 285);            // 0^2+1^2+...+9^2

	pool.join();
}
