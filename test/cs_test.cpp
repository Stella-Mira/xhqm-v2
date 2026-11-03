// cs 测试：通用 C/S 模板 + TCP 传输层（loopback 请求-响应）
#include "../network/cs.h"
#include "../network/tcp.h"
#include "test.h"
#include <atomic>
#include <thread>

using namespace xhqm;

static std::atomic<bool> g_listening{ false };

// 服务端线程：cs_server 阻塞监听，handler 做 echo
static void server_main()
{
	xhqm::network::tcp_server srv;
	if (!srv.listen(19527)) return;
	xhqm::network::cs_server<xhqm::network::tcp_server, xhqm::network::tcp_conn> server(
		std::move(srv),
		[](const std::string& req) -> std::string { return "echo:" + req; }
	);
	g_listening = true;
	server.run();   // 阻塞（测试结束时进程退出）
}

TEST_CASE("test_cs")
{
	std::thread svr(server_main);
	svr.detach();
	while (!g_listening) std::this_thread::sleep_for(std::chrono::milliseconds(5));

	xhqm::network::tcp_client c;
	CHECK(c.connect("127.0.0.1", 19527));
	xhqm::network::cs_client<xhqm::network::tcp_client> client(std::move(c));

	std::string resp;
	CHECK(client.request("hello", resp));
	CHECK(resp == "echo:hello");
	CHECK(client.request("世界", resp));
	CHECK(resp == "echo:世界");
	CHECK(client.request("", resp));
	CHECK(resp == "echo:");
}
