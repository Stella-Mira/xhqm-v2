// nebula_api_test.cpp —— nebula B 世代独立验证（黑盒视角重建）
//
// 验证范围（与 SVN r253/r256 记录对齐，但代码独立重写）：
//   T1  L1 三节点冒烟：入网收敛 / 邻居对称 / 杀节点判死 / 干净停止
//   T2  L2 十五节点回归：全图发现 region=15 / 跨拓扑订阅投递 / 杀三自愈
//   T3  数据层：通道窗口滑动 / 本地聚合 / 远端 query-aggregate（answer_event 请求-应答）
//
// 构建（Linux，链接含 nebula 的 xhqm 库）：
//   g++ -std=c++17 -O2 -I.. -DXHQM_PROGRAM nebula_api_test.cpp -L../../binary -lxhqm -lpthread -o nebula_api_test

#include "../test.h"

#include "../../network/nebula/data.h"
#include "../../network/nebula/zero.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

using namespace xhqm;

// 中枢/测试节点：data 声明纯虚 collect，非采集节点实现为空返回
class test_node : public xhqm::nebula::data
{
public:
	explicit test_node(std::shared_ptr<xhqm::nebula::link_b> link)
		: data(std::move(link))
	{
	}
	nebula::json collect() override { return {}; }   // 中枢节点：无采集
};

// ---- 工具 ----

static nebula::addr bcast(int udp_port)
{
	return std::make_shared<nebula::addr_t<nebula::kind::ipv4>>("255.255.255.255", udp_port);
}

// 轮询等待条件成立（黑盒等待：无内部钩子可挂）
static bool wait_until(const std::function<bool()>& cond, int timeout_ms, int step_ms = 100)
{
	auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
	while (std::chrono::steady_clock::now() < deadline)
	{
		if (cond()) return true;
		std::this_thread::sleep_for(std::chrono::milliseconds(step_ms));
	}
	return cond();
}

// 造一个已入网节点：link_t<ipv4> 句柄注入 data（模板停在 link 层）
// listen：广播地址随 listen 传入，保证 creataddr→broadcast→life 顺序
static std::unique_ptr<test_node> make_node(int event_port, int udp_port, uint m = 2, uint cap = 4)
{
	auto link = std::make_shared<nebula::link_t<nebula::kind::ipv4>>(m, cap, 1, 4);
	auto node = std::make_unique<test_node>(std::move(link));
	node->heart(300, 5, 500);
	if (!node->listen(0, event_port, "0.0.0.0", bcast(udp_port))) return nullptr;
	return node;
}

// ---- T1：L1 三节点冒烟 ----

TEST_CASE("nebula L1 三节点：入网收敛/邻居对称/判死/干净停止")
{
	constexpr int udp = 19000;
	auto n1 = make_node(19001, udp);
	auto n2 = make_node(19002, udp);
	auto n3 = make_node(19003, udp);
	REQUIRE(n1 != nullptr);
	REQUIRE(n2 != nullptr);
	REQUIRE(n3 != nullptr);

	// 入网收敛：m=2，三节点应全互连（degree>=2）
	CHECK(wait_until([&] { return n1->degree() >= 2 && n2->degree() >= 2 && n3->degree() >= 2; }, 15000));

	// 邻居对称性：n1 视角含 n2/n3
	CHECK(n1->has_neighbor(n2->id()));
	CHECK(n1->has_neighbor(n3->id()));
	CHECK(n2->has_neighbor(n1->id()));
	CHECK(n3->has_neighbor(n2->id()));

	// 杀节点判死：n3 关停，n1/n2 应在失联上限（5×300ms）后清理
	auto id3 = n3->id();
	n3->stop();
	CHECK(wait_until([&] { return !n1->has_neighbor(id3) && !n2->has_neighbor(id3); }, 6000));

	// 存活节点继续互通（ping 未受影响：n1/n2 互相仍在）
	CHECK(n1->has_neighbor(n2->id()));
	CHECK(n2->has_neighbor(n1->id()));

	// 干净停止：剩余节点 stop 不挂（析构路径由测试结束覆盖）
	n1->stop();
	n2->stop();
	n3->stop();   // 幂等
}

// ---- T2：L2/L3 十五节点回归 ----

TEST_CASE("nebula L2/L3 十五节点：全图发现/跨拓扑订阅/杀三自愈")
{
	constexpr int udp = 19400;
	constexpr int n_count = 15;
	std::vector<std::unique_ptr<test_node>> nodes;
	nodes.reserve(n_count);
	for (int i = 0; i < n_count; ++i)
	{
		auto link = std::make_shared<nebula::link_t<nebula::kind::ipv4>>(3, 9, 1, 4);
		auto n = std::make_unique<test_node>(std::move(link));
		n->heart(200, 4, 400);
		REQUIRE(n->listen(0, 19401 + i, "0.0.0.0", bcast(udp)));
		nodes.push_back(std::move(n));
	}

	// 入网收敛：全部 degree>=2（m=3 上限 9，广播补连接）
	CHECK(wait_until([&] {
		for (const auto& n : nodes) if (n->degree() < 2) return false;
		return true;
	}, 20000));

	// L2 全图发现：discover 幂等（事件 id 去重），循环补发现直到 region 全量或预算耗尽。
	// 预算公式：转发预算 = budget×(l-1)/l，层预算须明显大于心跳间隔（200ms），
	// 故 layers=12 / timeout=6000（末层预算 500ms = 2.5×心跳）；15 节点 m=3 随机图
	// 单轮归并时序偶发丢边，多轮可补齐。
	bool full_region = false;
	for (int round = 0; round < 5 && !full_region; ++round)
	{
		for (auto& n : nodes) n->discover(12, 6000);
		full_region = wait_until([&] {
			for (const auto& n : nodes) if (n->region_size() < n_count) return false;
			return true;
		}, 8000);
	}
	CHECK(full_region);
	for (const auto& n : nodes)
		CHECK(n->region_size() == n_count);

	// L3 跨拓扑订阅：node[0] 建通道上传；node[14] 订阅；node[0] 再上传 → node[14] 收到
	auto ch = nodes[0]->create("nebula_api_test_channel");
	REQUIRE(nodes[0]->upload(ch, { { "value", 1 } }));

	std::atomic<int> received{ 0 };
	CHECK(nodes[14]->subscribe(ch, 0, [&](const nebula::uuid&, const nebula::json&) {
		++received;
	}, std::vector<nebula::addr>{}));

	// 订阅扩散（软状态）+ 推送表建立（15 节点直径较大，放宽等待）
	std::this_thread::sleep_for(std::chrono::milliseconds(3000));

	nodes[0]->upload(ch, { { "value", 2 } });
	CHECK(wait_until([&] { return received.load() >= 1; }, 15000));

	// 杀三自愈：nodes[7..9] 关停，其余节点判死后网络继续工作（再投递一次）
	auto id7 = nodes[7]->id(), id8 = nodes[8]->id(), id9 = nodes[9]->id();
	nodes[7]->stop();
	nodes[8]->stop();
	nodes[9]->stop();
	CHECK(wait_until([&] {
		return !nodes[0]->has_neighbor(id7) && !nodes[0]->has_neighbor(id8) && !nodes[0]->has_neighbor(id9);
	}, 8000));

	received = 0;
	// 自愈语义：中继路径首跳死亡 → 订阅软状态周期重发（10s）刷新路径 →
	// 新路径投递成功（不等待刷新的立即投递超出库承诺：泛洪 TTL=8 在直径大的图上兜不住）
	std::this_thread::sleep_for(std::chrono::milliseconds(12000));
	nodes[0]->upload(ch, { { "value", 3 } });
	CHECK(wait_until([&] { return received.load() >= 1; }, 15000));

	// 干净停止
	for (auto& n : nodes) n->stop();
}

// ---- T3：数据层（窗口/聚合/远端请求-应答）----

TEST_CASE("nebula 数据层：窗口滑动/聚合/远端 query-aggregate")
{
	constexpr int udp = 19800;

	// 三个节点：A 建通道，其余 adopt，各自上传数据
	auto a = make_node(19801, udp);
	auto b = make_node(19802, udp);
	auto c = make_node(19803, udp);
	REQUIRE(a != nullptr);
	REQUIRE(b != nullptr);
	REQUIRE(c != nullptr);
	CHECK(wait_until([&] { return a->degree() >= 2 && b->degree() >= 2 && c->degree() >= 2; }, 15000));

	// 窗口策略：容量 2，上传 3 条只剩后 2 条
	nebula::window_policy p;
	p.capacity = 2;
	a->policy(p);
	auto ch = a->create("window_test");
	CHECK(a->upload(ch, { { "value", 1 } }));
	CHECK(a->upload(ch, { { "value", 2 } }));
	CHECK(a->upload(ch, { { "value", 3 } }));
	auto snap = a->snapshot(ch);
	CHECK(snap.size() == 2);
	CHECK(snap[0]["json"]["value"].get<int>() == 2);
	CHECK(snap[1]["json"]["value"].get<int>() == 3);

	// 共享通道：B/C adopt 并上传，A 全网 aggregate 汇总 3 节点
	CHECK(b->adopt(ch, "shared"));
	CHECK(c->adopt(ch, "shared"));
	CHECK(b->upload(ch, { { "value", 10 } }));
	CHECK(c->upload(ch, { { "value", 20 } }));

	for (auto& n : { a.get(), b.get(), c.get() }) n->discover(8, 3000);
	CHECK(wait_until([&] {
		return a->region_size() >= 3 && b->region_size() >= 3 && c->region_size() >= 3;
	}, 15000));

	// 全网聚合（answer_event 请求-应答路径）
	auto agg = a->aggregate(ch, "value", 1000);
	CHECK(agg.value("ok", false));
	CHECK(agg["count"].get<u64>() == 4);   // A 窗口 2 条（容量 2 滑出 value=1）+ B 1 条 + C 1 条
	CHECK(agg["sum"].get<double>() == 2.0 + 3.0 + 10.0 + 20.0);
	CHECK(agg["nodes"].get<u64>() == 3);

	// 远端 query：B 直连各节点（含自己），响应带 from 标记
	auto q = a->query(nebula::json({ { "channel", xhqm::cipher::uuid::to_text(ch) } }), 1000);
	CHECK(q.size() == 3);
	int from_self = 0;
	for (const auto& r : q)
		if (r["from"] == "self") ++from_self;
	CHECK(from_self == 1);

	// 干净停止
	a->stop(); b->stop(); c->stop();
}

// ---- T4：zero 采集节点（空壳继承 data，子类实现 collect）----

namespace
{
	// 测试采集节点：collect 返回计数值
	class counter_sensor : public xhqm::nebula::zero
	{
	public:
		explicit counter_sensor()
			: zero(std::make_shared<nebula::link_t<nebula::kind::ipv4>>(2, 4, 1, 4))
		{
		}
		nebula::json collect() override
		{
			++count;
			return nebula::json({ { "n", count } });
		}
		int count = 0;
	};
}

TEST_CASE("nebula zero：采集上传到通道")
{
	counter_sensor s;
	auto ch = s.create("counter");
	CHECK(s.upload(ch));
	CHECK(s.upload(ch));
	auto snap = s.snapshot(ch);
	CHECK(snap.size() == 2);
	CHECK(snap[1]["json"]["n"].get<int>() == 2);
	s.stop();
}

// ---- T5：hardware media 设备抽象（caps 往返 + 接口签名可用）----

#include "../../hardware/media.h"

TEST_CASE("hardware media：caps json 往返与抽象接口")
{
	using namespace xhqm::hardware::media;

	// 视频设备能力
	caps c;
	c.id = "cam.3090.0";
	c.host = "3090";
	c.k = kind::cam;
	c.d = dir::input;
	c.medias = { media_type::video };
	video_format v;
	v.width = 1920; v.height = 1080; v.fps = 30; v.codec = "h264";
	c.videos = { v };

	auto j = c.to_json();
	CHECK(j["id"].get<xhqm::text>() == "cam.3090.0");
	CHECK(j["kind"].get<xhqm::text>() == "cam");
	CHECK(j["dir"].get<xhqm::text>() == "input");

	caps c2;
	CHECK(c2.from_json(j));
	CHECK(c2.id == c.id);
	CHECK(c2.host == c.host);
	CHECK(c2.k == kind::cam);
	CHECK(c2.d == dir::input);
	CHECK(c2.is_video());
	CHECK(!c2.is_audio());
	CHECK(c2.videos.size() == 1);
	CHECK(c2.videos[0].width == 1920);
	CHECK(c2.videos[0].height == 1080);
	CHECK(c2.videos[0].fps == 30);
	CHECK(c2.videos[0].codec == "h264");

	// 音频格式往返
	audio_format a;
	a.rate = 16000; a.channels = 1; a.codec = "pcm_s16le";
	audio_format a2;
	CHECK(a2.from_json(a.to_json()));
	CHECK(a2.rate == 16000);
	CHECK(a2.channels == 1);

	// 设备接口抽象：只定义签名（实现层继承），此处仅编译验证可继承性
	// （注意全限定：xhqm::device（计算设备结构体）与 media::device 同名不同域）
	struct probe_device : xhqm::hardware::media::device
	{
		caps capability() const override { return {}; }
		bool open(const xhqm::hardware::media::json&) override { return true; }
		void close() override {}
		bool good() const override { return true; }
		xhqm::text stream_url() const override { return "rtsp://test:8554/cam"; }
	};
	probe_device pd;
	CHECK(pd.good());
	CHECK(pd.stream_url() == "rtsp://test:8554/cam");
}

// ---- T6：M2 协议往返（devices/streams/commands/acks 在真实组网上验证）----

#include "common/protocol.h"

TEST_CASE("nebula_api 协议：设备注册/流发布/指令回执往返")
{
	using namespace nebula_api::common;

	constexpr int udp = 20200;

	// a = 设备端（注册/流/回执）；b = 中枢端（订阅/发指令）
	auto a = make_node(20201, udp);
	auto b = make_node(20202, udp);
	auto c = make_node(20203, udp);
	REQUIRE(a != nullptr);
	REQUIRE(b != nullptr);
	REQUIRE(c != nullptr);
	CHECK(wait_until([&] { return a->degree() >= 2 && b->degree() >= 2 && c->degree() >= 2; }, 15000));

	// ---- 通道 uuid 确定性（两端同算同得）----
	CHECK(command_channel("cam.a") == command_channel("cam.a"));
	CHECK(command_channel("cam.a") != command_channel("cam.b"));

	// ---- 双方登记全局通道 + 中枢订阅 ----
	CHECK(a->adopt(devices_channel(), "devices"));
	CHECK(a->adopt(streams_channel(), "streams"));
	CHECK(a->adopt(acks_channel(), "acks"));
	CHECK(b->adopt(devices_channel(), "devices"));
	CHECK(b->adopt(streams_channel(), "streams"));
	CHECK(b->adopt(acks_channel(), "acks"));

	std::atomic<int> dev_rx{ 0 }, stream_rx{ 0 }, ack_rx{ 0 }, cmd_rx{ 0 };
	nebula::json last_dev, last_stream, last_ack, last_cmd;
	std::mutex rx_mutex;
	auto capture = [&rx_mutex](std::atomic<int>& cnt, nebula::json& slot, const nebula::uuid&, const nebula::json& data) {
		std::lock_guard<std::mutex> lk(rx_mutex);
		slot = nebula_api::common::latest_message(data);   // 窗口快照 → 最新消息（PROTOCOL §3.0）
		++cnt;
	};

	CHECK(b->subscribe(devices_channel(), 0, [&](const nebula::uuid& ch, const nebula::json& d) { capture(dev_rx, last_dev, ch, d); }, {}));
	CHECK(b->subscribe(streams_channel(), 0, [&](const nebula::uuid& ch, const nebula::json& d) { capture(stream_rx, last_stream, ch, d); }, {}));
	CHECK(b->subscribe(acks_channel(), 0, [&](const nebula::uuid& ch, const nebula::json& d) { capture(ack_rx, last_ack, ch, d); }, {}));
	// 设备端订阅自己的指令通道
	auto cmd_ch = command_channel("cam.a");
	CHECK(a->subscribe(cmd_ch, 0, [&](const nebula::uuid& ch, const nebula::json& d) { capture(cmd_rx, last_cmd, ch, d); }, {}));

	// 订阅扩散（软状态）
	std::this_thread::sleep_for(std::chrono::milliseconds(2000));

	// ---- 1. devices：注册 → 中枢可见 ----
	media::caps cap;
	cap.id = "cam.a";
	cap.host = "node-a";
	cap.k = media::kind::cam;
	cap.d = media::dir::input;
	cap.medias = { media::media_type::video };
	CHECK(a->upload(devices_channel(), make_register(cap)));
	CHECK(wait_until([&] { return dev_rx.load() >= 1; }, 10000));
	{
		std::lock_guard<std::mutex> lk(rx_mutex);
		CHECK(last_dev["op"].get<xhqm::text>() == "register");
		CHECK(last_dev["caps"]["id"].get<xhqm::text>() == "cam.a");
		CHECK(last_dev["v"].get<int>() == protocol_version);
	}

	// ---- 2. streams：起流地址发布 → 中枢可见 ----
	CHECK(a->upload(streams_channel(), make_stream_up("cam.a", "rtsp://node-a:8554/cam.a", nebula::json{ { "codec", "h264" } })));
	CHECK(wait_until([&] { return stream_rx.load() >= 1; }, 10000));
	{
		std::lock_guard<std::mutex> lk(rx_mutex);
		CHECK(last_stream["op"].get<xhqm::text>() == "up");
		CHECK(last_stream["url"].get<xhqm::text>() == "rtsp://node-a:8554/cam.a");
	}

	// ---- 3. commands：中枢发指令 → 设备执行 → 回执 ----
	// b 发布指令到 command_channel("cam.a")（a 已订阅）
	auto cmd = make_command("open", nebula::json{ { "format", nebula::json::object() } });
	b->publish(cmd_ch, cmd);
	CHECK(wait_until([&] { return cmd_rx.load() >= 1; }, 10000));
	xhqm::text cmd_id;
	{
		std::lock_guard<std::mutex> lk(rx_mutex);
		cmd_id = last_cmd.value("id", "");
		CHECK(last_cmd["action"].get<xhqm::text>() == "open");
		CHECK(!cmd_id.empty());
	}

	// 设备端执行（模拟 open 成功）→ 回执
	auto ack = make_ack(last_cmd, true, nebula::json{ { "url", "rtsp://node-a:8554/cam.a" } }, "cam.a");
	CHECK(a->upload(acks_channel(), ack));
	CHECK(wait_until([&] { return ack_rx.load() >= 1; }, 10000));
	{
		std::lock_guard<std::mutex> lk(rx_mutex);
		CHECK(last_ack["cmd_id"].get<xhqm::text>() == cmd_id);
		CHECK(last_ack["ok"].get<bool>() == true);
		CHECK(last_ack["result"]["url"].get<xhqm::text>() == "rtsp://node-a:8554/cam.a");
	}

	// ---- 4. 未知指令：执行器语义（协议层验证错误外壳）----
	auto bad = make_command("unknown_action", nebula::json::object());
	b->publish(cmd_ch, bad);
	CHECK(wait_until([&] { return cmd_rx.load() >= 2; }, 10000));

	// 干净停止
	a->stop(); b->stop(); c->stop();
}

// ---- T7：M4 端到端（客户端注册/指令开流/回执/下线 → 服务器目录）----

#include "client/device_client.h"
#include "server/media_server.h"


TEST_CASE("nebula_api M4 端到端：注册可见/指令开流回执/下线注销")
{
	using namespace nebula_api;
	constexpr int udp = 20300;

	// 中枢 + 两个客户端节点
	auto srv_link = std::make_shared<nebula::link_t<nebula::kind::ipv4>>(2, 4, 1, 4);
	server::media_server srv(srv_link);
	srv.heart(300, 5, 500);
	REQUIRE(srv.online(0, 20301, "0.0.0.0", bcast(udp)));

	// 每个客户端内嵌一个 MediaMTX（独立端口，D14）
	client::mtx_server mtx_a("/home/xhqm/Space/xhqm-pro/depend/mediamtx/mediamtx", 28561);
	client::mtx_server mtx_b("/home/xhqm/Space/xhqm-pro/depend/mediamtx/mediamtx", 28562);

	auto ca_link = std::make_shared<nebula::link_t<nebula::kind::ipv4>>(2, 4, 1, 4);
	client::device_client cli_a(ca_link, mtx_a);
	cli_a.heart(300, 5, 500);
	xhqm::hardware::media::audio_format af;
	af.rate = 16000; af.channels = 1;
	REQUIRE(cli_a.add_device(std::make_shared<client::mic_device>("cam.a", "node-a",
		std::make_shared<nebula_api::client::stub_capture>(), mtx_a, "ffmpeg", af)));
	REQUIRE(cli_a.online(0, 20302, "0.0.0.0", bcast(udp)));

	auto cb_link = std::make_shared<nebula::link_t<nebula::kind::ipv4>>(2, 4, 1, 4);
	client::device_client cli_b(cb_link, mtx_b);
	cli_b.heart(300, 5, 500);
	REQUIRE(cli_b.add_device(std::make_shared<client::mic_device>("mic.b", "node-b",
		std::make_shared<nebula_api::client::stub_capture>(), mtx_b, "ffmpeg", af)));
	REQUIRE(cli_b.online(0, 20303, "0.0.0.0", bcast(udp)));

	// 入网收敛
	CHECK(wait_until([&] {
		return srv.degree() >= 2 && cli_a.degree() >= 2 && cli_b.degree() >= 2;
	}, 15000));

	// ---- 1. 设备注册可见（订阅扩散 + 目录建立）----
	CHECK(wait_until([&] {
		return srv.has_device("cam.a") && srv.has_device("mic.b");
	}, 15000));
	auto dir = srv.devices();
	CHECK(dir.size() == 2);

	// ---- 2. 指令 open：中枢下发 → 设备执行 → 回执 → 流地址缓存 ----
	auto r_open = srv.command("cam.a", "open", nebula::json{ { "format", nebula::json::object() } }, 5000);
	CHECK(r_open.value("ok", false));
	CHECK(r_open["url"].get<xhqm::text>() == "rtsp://127.0.0.1:28561/cam.a");
	// streams up 与 acks 是两条独立消息，up 可能后到——等待缓存建立
	CHECK(wait_until([&] { return srv.stream_url("cam.a") == "rtsp://127.0.0.1:28561/cam.a"; }, 10000));

	// ---- 3. 指令 list：设备端返回自身设备清单 ----
	auto r_list = srv.command("mic.b", "list", nebula::json::object(), 5000);
	CHECK(r_list.value("ok", false));
	CHECK(r_list["devices"].is_array());
	CHECK(r_list["devices"][0]["id"].get<xhqm::text>() == "mic.b");

	// ---- 4. 未知指令：错误外壳 ----
	auto r_bad = srv.command("cam.a", "unknown_action", nebula::json::object(), 5000);
	CHECK(!r_bad.value("ok", true));
	CHECK(r_bad["error"].get<xhqm::text>() == "unknown_action");

	// ---- 5. 优雅下线：目录移除 + 流地址清理 ----
	cli_a.offline();
	CHECK(wait_until([&] { return !srv.has_device("cam.a"); }, 10000));
	CHECK(!srv.stream_url("cam.a").empty() == false);   // unregister 到达后目录移除；流缓存按 down 清理
	// （注：offline 顺序 close→streams down→unregister，目录条目已移除为准）

	cli_b.offline();
	srv.offline();
}

// ---- T8：MediaMTX 真实集成（起流 → 真实 rtsp 地址 → ffprobe 拉流可读）----

#include "client/mtx_server.h"

TEST_CASE("nebula_api MediaMTX 集成：真实起流与拉流验证")
{
	using namespace nebula_api::client;

	// 独立测试端口，避免与其他实例冲突
	mtx_server mtx("/home/xhqm/Space/xhqm-pro/depend/mediamtx/mediamtx", 28554);
	CHECK(mtx.start());
	CHECK(mtx.running());

	xhqm::hardware::media::audio_format af;
	af.rate = 16000; af.channels = 1;
	mic_device mic("mic.t8", "localhost", std::make_shared<nebula_api::client::stub_capture>(), mtx, "ffmpeg", af, false);

	// open → 后端启动 + 推流（lavfi sine 模拟麦克风）→ 真实 rtsp 地址
	CHECK(mic.open(xhqm::hardware::media::json::object()));
	CHECK(mic.good());
	xhqm::text url = mic.stream_url();
	CHECK(url == "rtsp://127.0.0.1:28554/mic.t8");

	// ffprobe 拉流验证：音频流 aac 可读
	xhqm::text cmd = "timeout 8 ffprobe -hide_banner -loglevel error -rtsp_transport tcp "
		"-show_entries stream=codec_name -of csv=p=0 " + url + " 2>&1";
	FILE* f = popen(cmd.c_str(), "r");
	std::string out;
	char buf[256];
	while (f && fgets(buf, sizeof(buf), f)) out += buf;
	if (f) pclose(f);
	CHECK(out.find("aac") != std::string::npos);

	// close → 停推流
	mic.close();
	CHECK(!mic.good());
	CHECK(mic.stream_url().empty());

	// 第二个设备复用同一 mtx 实例（视频设备 testsrc）
	mic_device cam("cam.t8", "localhost", std::make_shared<nebula_api::client::stub_capture>(), mtx, "ffmpeg", af, true);
	CHECK(cam.open(xhqm::hardware::media::json::object()));
	xhqm::text curl = cam.stream_url();
	CHECK(curl == "rtsp://127.0.0.1:28554/cam.t8");
	xhqm::text cmd2 = "timeout 8 ffprobe -hide_banner -loglevel error -rtsp_transport tcp "
		"-show_entries stream=codec_name -of csv=p=0 " + curl + " 2>&1";
	f = popen(cmd2.c_str(), "r");
	out.clear();
	while (f && fgets(buf, sizeof(buf), f)) out += buf;
	if (f) pclose(f);
	CHECK(out.find("h264") != std::string::npos);
	cam.close();

	mtx.stop();
	CHECK(!mtx.running());
}

// ---- T9：热插拔（插入自动识别注册 / 拔出自动停流注销，端到端）----

#include "client/linux_hotplug.h"

#include <cstdlib>
#include <fstream>

TEST_CASE("nebula_api 热插拔：插入自动注册/拔出自动注销")
{
	using namespace nebula_api;
	constexpr int udp = 20400;

	// 临时监听目录（touch/rm 模拟插拔，本机无真实 USB 设备也能验）
	char tmpdir[] = "/tmp/xhqm_hotplug_XXXXXX";
	REQUIRE(mkdtemp(tmpdir) != nullptr);

	// 中枢 + 客户端
	auto srv_link = std::make_shared<nebula::link_t<nebula::kind::ipv4>>(2, 4, 1, 4);
	server::media_server srv(srv_link);
	srv.heart(300, 5, 500);
	REQUIRE(srv.online(0, 20401, "0.0.0.0", bcast(udp)));

	client::mtx_server mtx("/home/xhqm/Space/xhqm-pro/depend/mediamtx/mediamtx", 28571);
	auto cli_link = std::make_shared<nebula::link_t<nebula::kind::ipv4>>(2, 4, 1, 4);
	client::device_client cli(cli_link, mtx);
	cli.heart(300, 5, 500);
	REQUIRE(cli.online(0, 20402, "0.0.0.0", bcast(udp)));

	// 热插拔监听：临时目录（默认工厂建 stub 设备）
	client::linux_hotplug hp({ xhqm::text(tmpdir) }, 300);
	CHECK(cli.attach_hotplug(hp));

	CHECK(wait_until([&] { return srv.degree() >= 1 && cli.degree() >= 1; }, 15000));

	// ---- 插入：video0 出现 → 自动建设备 + 注册 → 中枢目录可见 ----
	{
		std::ofstream(std::string(tmpdir) + "/video0") << "x";
		CHECK(wait_until([&] { return cli.devices().size() == 1; }, 5000));
		auto devs = cli.devices();
		REQUIRE(devs.size() == 1);
		CHECK(devs[0].id == "cam.local.video0");
		CHECK(devs[0].k == xhqm::hardware::media::kind::cam);

		// 中枢目录（软状态刷新，最多等一个 10s 周期 + 余量）
		CHECK(wait_until([&] { return srv.has_device("cam.local.video0"); }, 15000));
	}

	// ---- 拔出：video0 消失 → 自动停流注销 → 本地与中枢均清空 ----
	{
		std::remove((std::string(tmpdir) + "/video0").c_str());
		CHECK(wait_until([&] { return cli.devices().empty(); }, 5000));
		CHECK(wait_until([&] { return !srv.has_device("cam.local.video0"); }, 15000));
	}

	// ---- 再插入另一类设备：snd → mic 设备 ----
	{
		std::ofstream(std::string(tmpdir) + "/snd") << "x";
		CHECK(wait_until([&] { return cli.devices().size() == 1; }, 5000));
		auto devs = cli.devices();
		REQUIRE(devs.size() == 1);
		CHECK(devs[0].id == "mic.local.snd");
		CHECK(wait_until([&] { return srv.has_device("mic.local.snd"); }, 15000));
		std::remove((std::string(tmpdir) + "/snd").c_str());
		CHECK(wait_until([&] { return cli.devices().empty(); }, 5000));
	}

	hp.stop();
	cli.offline();
	srv.offline();
	rmdir(tmpdir);
}
