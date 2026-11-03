// 序列化 + 有向图节点测试：tensor_payload（json 存 dtype/shape/原始字节）round-trip、
// directed_node 来/去连接、拓扑排序。
#include "../algebra/soul/serialize.h"
#include "../lattice/directed_mesh.hpp"
#include "../lattice/graph_algorithms.hpp"
#include "test.h"
#include <array>
#include <vector>
#include <string>

using namespace xhqm;
using namespace xhqm::soul;

TEST_CASE("test_directed_node")
{
	using node_t = xhqm::_graph_::directed_node<std::string>;
	node_t a(std::string("a"));
	node_t b(std::string("b"));
	node_t c(std::string("c"));

	a.add_goto(&c);   // a → c
	b.add_goto(&c);   // b → c

	// 来连接：c 的输入是 a, b
	CHECK(c.in_degree() == 2);
	CHECK((c.in_from(&a) && c.in_from(&b)));
	CHECK(c.from_list().size() == 2);
	// 去连接：a, b 的输出是 c
	CHECK((a.out_degree() == 1 && a.goto_list()[0] == &c));
	CHECK((b.out_degree() == 1 && b.goto_list()[0] == &c));
	// 源/汇
	CHECK((a.is_source() && b.is_source()));
	CHECK(c.is_sink());

	// 拓扑排序：边 = 数据流 a→c, b→c，c 必须最后
	std::vector<node_t*> nodes = { &c, &a, &b };  // 乱序输入
	auto order = xhqm::topological_sort(nodes);
	CHECK(order.size() == 3);
	CHECK(order.back() == &c);

	// 反向建边等价：c.add_from(a) 已在 add_goto 中登记，重复不叠加
	CHECK(c.add_from(&a) == false);   // 已存在

	// 拆边
	CHECK(a.sub_goto(&c) == true);
	CHECK((c.in_degree() == 1 && !c.in_from(&a)));
	CHECK(a.out_degree() == 0);
	CHECK(a.sub_goto(&c) == false);   // 已拆
}

TEST_CASE("test_serialize_roundtrip")
{
	// f32 2D
	tensor<2, f32> t(std::array<xhqm::size, 2>{ 2, 3 }, 0.0f);
	for (xhqm::size i = 0; i < 6; ++i) t[i] = float(i) * 0.5f;

	tensor_payload<2, f32> p(t);
	nlohmann::json j = p.save_json();
	CHECK(j.at("dtype").get<std::string>() == "f32");
	CHECK(j.at("dim").get<xhqm::size>() == 2);
	auto shape = j.at("shape").get<std::vector<xhqm::size>>();
	CHECK((shape.size() == 2 && shape[0] == 2 && shape[1] == 3));

	tensor_payload<2, f32> q;
	q.read_json(j);
	for (xhqm::size i = 0; i < 6; ++i)
		CHECK_NEAR(q.value[i], t[i], 1e-6);

	// json → cbor → json 二进制往返无损
	auto bytes = json_to_cbor(j);
	CHECK(!bytes.empty());
	tensor_payload<2, f32> r;
	r.read_json(cbor_to_json(bytes));
	for (xhqm::size i = 0; i < 6; ++i)
		CHECK_NEAR(r.value[i], t[i], 1e-6);
}

TEST_CASE("test_serialize_f64")
{
	// f64 1D
	tensor<1, f64> d(std::array<xhqm::size, 1>{ 4 }, 0.0);
	for (xhqm::size i = 0; i < 4; ++i) d[i] = double(i) + 0.25;

	tensor_payload<1, f64> pd(d);
	tensor_payload<1, f64> pd2;
	pd2.read_json(cbor_to_json(json_to_cbor(pd.save_json())));
	CHECK(pd2.value.dimension(0) == 4);
	for (xhqm::size i = 0; i < 4; ++i)
		CHECK_NEAR(pd2.value[i], d[i], 1e-12);
}

TEST_CASE("test_serialize_mismatch")
{
	tensor<1, f32> a(std::array<xhqm::size, 1>{ 3 }, 1.0f);
	tensor_payload<1, f32> pa(a);
	nlohmann::json j = pa.save_json();

	// dim 不匹配（用 2 维 payload 读 1 维数据）应抛错
	bool threw = false;
	try {
		tensor_payload<2, f32> wrong;
		wrong.read_json(j);
	} catch (const xhqm::error&) { threw = true; }
	CHECK(threw);
}
