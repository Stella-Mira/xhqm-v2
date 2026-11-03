// ECS 测试：tuple_values 组件存储 / model_ecs 挂载闭环 / 多组件 join / theory_instancing 帧图接入 / 批量变换 benchmark 对比树遍历
// 设计依据：fantasy/README.md「数据规则（ECS 化，设计文档）」
#include "../metaclass/tuple_values.h"
#include "../fantasy/model_ecs.h"
#include "../fantasy/theory_render.h"
#include "test.h"
#include <chrono>
#include <iostream>

using namespace xhqm;

TEST_CASE("test_tuple_values_soa")
{
	//主模板：多列 SoA（position 三列）
	tuple_values<float, float, float> positions;
	CHECK(positions.empty());

	positions.push(1.0f, 2.0f, 3.0f);
	positions.push(4.0f, 5.0f, 6.0f);
	CHECK(positions.size() == 2);

	//读（值返回，聚合还原语义类型）
	fantasy::point p = positions.value<fantasy::point>(0);
	CHECK_NEAR(p.x(), 1.0, 1e-6);
	CHECK_NEAR(p.y(), 2.0, 1e-6);
	CHECK_NEAR(p.z(), 3.0, 1e-6);

	//读（值返回，取单列）
	float col_y = positions.value<float, 1>(1);
	CHECK_NEAR(col_y, 5.0, 1e-6);

	//读（out 参数，零构造）
	float x = 0, y = 0, z = 0;
	CHECK(positions.get_value(1, x, y, z));
	CHECK((x == 4.0f && y == 5.0f && z == 6.0f));
	CHECK(!positions.get_value(2, x, y, z));   // 越界返回 false

	//写（整体写每列）
	CHECK(positions.set_value(0, 7.0f, 8.0f, 9.0f));
	CHECK(!positions.set_value(2, 0.0f, 0.0f, 0.0f));
	positions.get_value(0, x, y, z);
	CHECK((x == 7.0f && y == 8.0f && z == 9.0f));

	//交换删除（与末尾交换后弹出，不保持顺序）
	CHECK(positions.erase(0));
	CHECK(positions.size() == 1);
	positions.get_value(0, x, y, z);
	CHECK((x == 4.0f && y == 5.0f && z == 6.0f));   // 原末尾交换到 0 位
	CHECK(!positions.erase(1));

	positions.clear();
	CHECK(positions.empty());
}

TEST_CASE("test_tuple_values_aos")
{
	//偏特化：单列 AoS（point 直接存储，可引用读写）
	tuple_values<fantasy::point> positions;
	positions.push(fantasy::point(1.0f, 2.0f, 3.0f));
	positions.push(fantasy::point(4.0f, 5.0f, 6.0f));
	CHECK(positions.size() == 2);

	//引用读/写（零构造）
	positions.value_ref(0).x() = 10.0f;
	CHECK_NEAR(positions.value_ref(0).x(), 10.0, 1e-6);
	CHECK_NEAR(positions.value(1).y(), 5.0, 1e-6);

	//out 参数 / 整体写
	fantasy::point p;
	CHECK(positions.get_value(1, p));
	CHECK_NEAR(p.z(), 6.0, 1e-6);
	CHECK(positions.set_value(1, fantasy::point(7.0f, 8.0f, 9.0f)));
	CHECK_NEAR(positions.value(1).z(), 9.0, 1e-6);

	//交换删除
	CHECK(positions.erase(0));
	CHECK(positions.size() == 1);
	CHECK_NEAR(positions.value_ref(0).x(), 7.0, 1e-6);
}

TEST_CASE("test_tuple_values_three_ways")
{
	//三类兼容（设计文档）：同一语义类型 point 的三种存储写法
	tuple_values<float, float, float> soa;          // 3 列 SoA：value<point>(i) 聚合
	tuple_values<fantasy::point> aos;               // 单列 AoS：value_ref(i) 引用
	soa.push(1.0f, 2.0f, 3.0f);
	aos.push(fantasy::point(1.0f, 2.0f, 3.0f));

	fantasy::point p_soa = soa.value<fantasy::point>(0);
	const fantasy::point& p_aos = aos.value_ref(0);
	CHECK_NEAR(p_soa.x(), p_aos.x(), 1e-6);
	CHECK_NEAR(p_soa.y(), p_aos.y(), 1e-6);
	CHECK_NEAR(p_soa.z(), p_aos.z(), 1e-6);
}

TEST_CASE("test_model_ecs_attach_detach")
{
	fantasy::model_ecs ecs;
	CHECK(ecs.entity_size() == 0);
	CHECK(ecs.handle_tuple(0) == nullptr);

	//新建并挂载（model_ecs 知道自己的数据该给谁管理）
	auto* positions = ecs.create_tuple<float, float, float>();
	CHECK(positions != nullptr);
	CHECK(ecs.entity_size() == 0);
	positions->values().push(1.0f, 2.0f, 3.0f);
	positions->values().push(4.0f, 5.0f, 6.0f);
	CHECK(ecs.entity_size() == 2);

	//同类型重复 create 返回既有存储
	CHECK(ecs.create_tuple<float, float, float>() == positions);

	//按类型取回 / 按位置取回
	CHECK(ecs.handle_tuple<float, float, float>() == positions);
	CHECK(ecs.handle_tuple<fantasy::point>() == nullptr);
	CHECK(ecs.handle_tuple(0) == static_cast<fantasy::ecs_tuple*>(positions));

	//detach(pos) 归还所有权（调用方负责释放）
	fantasy::ecs_tuple* back = ecs.detach((xhqm::index)0);
	CHECK(back == static_cast<fantasy::ecs_tuple*>(positions));
	CHECK(ecs.handle_tuple(0) == nullptr);
	delete back;

	//attach 取所有权 / detach(ptr) 归还
	auto* colors = new fantasy::ecs_tuple_of<unsigned int>();
	CHECK(ecs.attach(colors) == 0);
	ecs.detach(colors);
	CHECK(ecs.handle_tuples().empty());
	delete colors;

	//model_ecs 析构释放所持存储（此处 create_tuple 挂载的由析构 delete_tuples）
	ecs.create_tuple<float, float, float>()->values().push(0.0f, 0.0f, 0.0f);
	CHECK(ecs.entity_size() == 1);
}

TEST_CASE("test_model_ecs_sync_instances")
{
	//SoA 三列 position → 实例平移矩阵
	{
		fantasy::model_ecs ecs;
		auto* positions = ecs.create_tuple<float, float, float>();
		positions->values().push(1.0f, 2.0f, 3.0f);
		positions->values().push(4.0f, 5.0f, 6.0f);

		auto* mesh = new fantasy::prefab();
		ecs.insert_drawer(mesh);

		CHECK(ecs.sync_instances());
		const std::vector<fantasy::matrix>& instances = mesh->instances();
		CHECK(instances.size() == 2);
		CHECK_NEAR(instances[0](0, 3), 1.0, 1e-6);
		CHECK_NEAR(instances[0](1, 3), 2.0, 1e-6);
		CHECK_NEAR(instances[0](2, 3), 3.0, 1e-6);
		CHECK_NEAR(instances[1](0, 3), 4.0, 1e-6);
		CHECK_NEAR(instances[1](2, 3), 6.0, 1e-6);
	}

	//AoS 单列 point → 实例平移矩阵
	{
		fantasy::model_ecs ecs;
		auto* positions = ecs.create_tuple<fantasy::point>();
		positions->values().push(fantasy::point(7.0f, 8.0f, 9.0f));

		auto* mesh = new fantasy::prefab();
		ecs.insert_drawer(mesh);

		CHECK(ecs.sync_instances());
		const std::vector<fantasy::matrix>& instances = mesh->instances();
		CHECK(instances.size() == 1);
		CHECK_NEAR(instances[0](0, 3), 7.0, 1e-6);
		CHECK_NEAR(instances[0](1, 3), 8.0, 1e-6);
		CHECK_NEAR(instances[0](2, 3), 9.0, 1e-6);
	}

	//无 position 组件：同步失败
	{
		fantasy::model_ecs ecs;
		CHECK(!ecs.sync_instances());
	}
}

TEST_CASE("test_tuple_join")
{
	//双组件 join：position += velocity * dt（存储层自由函数，热路径零构造读取）
	tuple_values<float, float, float> positions;
	tuple_values<float, float, float> velocities;
	positions.push(0.0f, 0.0f, 0.0f);
	positions.push(1.0f, 1.0f, 1.0f);
	positions.push(2.0f, 2.0f, 2.0f);
	velocities.push(1.0f, 0.0f, 0.0f);
	velocities.push(0.0f, 1.0f, 0.0f);   // 故意少一个实体：联合容量取最小值

	CHECK(join_size(positions, velocities) == 2);

	const float dt = 0.5f;
	join_each(positions, velocities,
		[&](xhqm::index pos, float px, float py, float pz, float vx, float vy, float vz) {
			positions.set_value(pos, px + vx * dt, py + vy * dt, pz + vz * dt);
		});

	float x = 0, y = 0, z = 0;
	positions.get_value(0, x, y, z);
	CHECK((x == 0.5f && y == 0.0f && z == 0.0f));
	positions.get_value(1, x, y, z);
	CHECK((x == 1.0f && y == 1.5f && z == 1.0f));
	positions.get_value(2, x, y, z);
	CHECK((x == 2.0f && y == 2.0f && z == 2.0f));   // 无 velocity 组件，未被 join 触及

	//三组件 join：AoS point + AoS color + SoA 标量混组
	tuple_values<fantasy::point> points;
	tuple_values<unsigned int> colors;
	tuple_values<float> scales;
	points.push(fantasy::point(1.0f, 2.0f, 3.0f));
	colors.push(0xff0000ffu);
	scales.push(2.0f);
	CHECK(join_size(points, colors, scales) == 1);

	xhqm::count visited = 0;
	join_each(points, colors, scales,
		[&](xhqm::index pos, fantasy::point p, unsigned int c, float s) {
			++visited;
			CHECK(pos == 0);
			CHECK_NEAR(p.x(), 1.0, 1e-6);
			CHECK_NEAR(p.z(), 3.0, 1e-6);
			CHECK(c == 0xff0000ffu);
			CHECK_NEAR(s, 2.0, 1e-6);
		});
	CHECK(visited == 1);
}

TEST_CASE("test_model_ecs_join")
{
	//model_ecs 层 join：组件身份用类型（position 用 point、velocity 用 vector 语义类型区分）
	fantasy::model_ecs ecs;
	auto* positions = ecs.create_tuple<fantasy::point>();
	auto* velocities = ecs.create_tuple<fantasy::vector>();
	positions->values().push(fantasy::point(0.0f, 0.0f, 0.0f));
	positions->values().push(fantasy::point(1.0f, 1.0f, 1.0f));
	velocities->values().push(fantasy::vector(1.0f, 0.0f, 0.0f));
	velocities->values().push(fantasy::vector(0.0f, 1.0f, 0.0f));

	const float dt = 1.0f;
	bool joined = ecs.join_each<fantasy::ecs_tuple_of<fantasy::point>, fantasy::ecs_tuple_of<fantasy::vector>>(
		[&](xhqm::index pos, fantasy::point p, fantasy::vector v) {
			positions->values().set_value(pos,
				fantasy::point(p.x() + v.x() * dt, p.y() + v.y() * dt, p.z() + v.z() * dt));
		});
	CHECK(joined);
	CHECK_NEAR(positions->values().value_ref(0).x(), 1.0, 1e-6);
	CHECK_NEAR(positions->values().value_ref(1).y(), 2.0, 1e-6);

	//缺组件存储：返回 false，不执行回调
	bool missing = ecs.join_each<fantasy::ecs_tuple_of<fantasy::point>, fantasy::ecs_tuple_of<unsigned int>>(
		[](xhqm::index, fantasy::point, unsigned int) { CHECK(false); });
	CHECK(!missing);
}

TEST_CASE("test_theory_instancing")
{
	//theory_instancing 接入 frame_graph：pass 驱动 model_ecs 批量提交（读 SoA → prefab 实例列表）
	fantasy::system* root = new fantasy::system();
	auto* ecs = new fantasy::model_ecs();
	root->add_child_ptr(ecs);
	auto* positions = ecs->create_tuple<float, float, float>();
	positions->values().push(1.0f, 2.0f, 3.0f);
	auto* mesh = new fantasy::prefab();
	ecs->insert_drawer(mesh);

	fantasy::theory_frame_graph fg;
	fantasy::theory_instancing instancing(root);
	instancing.fg(&fg);
	fg.register_pass(&instancing);

	CHECK(mesh->instances().empty());   // 尚未同步
	fg.run();                           // instancing pass：批量提交
	CHECK(mesh->instances().size() == 1);
	CHECK_NEAR(mesh->instances()[0](0, 3), 1.0, 1e-6);
	CHECK_NEAR(mesh->instances()[0](2, 3), 3.0, 1e-6);

	//数据版本守卫：数据未变再跑 pass，实例列表不重建（同步去重）
	fg.run();
	CHECK(mesh->instances().size() == 1);

	//数据变更（set_value 自动升版本）→ 下一帧 pass 重新同步
	positions->values().set_value(0, 4.0f, 5.0f, 6.0f);
	fg.run();
	CHECK_NEAR(mesh->instances()[0](0, 3), 4.0, 1e-6);
	CHECK_NEAR(mesh->instances()[0](1, 3), 5.0, 1e-6);

	//实体追加（push 自动升版本）→ 下一帧 pass 同步出新实例
	positions->values().push(7.0f, 8.0f, 9.0f);
	fg.run();
	CHECK(mesh->instances().size() == 2);
	CHECK_NEAR(mesh->instances()[1](0, 3), 7.0, 1e-6);

	delete ecs;    // model_ecs 析构释放所持 tuple 与 drawer
	delete root;
}

TEST_CASE("test_ecs_benchmark")
{
	//单组件最小例子：批量变换 benchmark 对比树遍历（设计文档落地顺序第 3 步）
	constexpr xhqm::count N = 100000;

	//树遍历路径：root + N 个 models 子节点，各自 coord 平移，traverse 传播世界矩阵
	fantasy::system* root = new fantasy::system();
	std::vector<fantasy::models*> nodes;
	nodes.reserve(N);
	for (xhqm::count i = 0; i < N; ++i)
	{
		auto* node = new fantasy::models();
		//models 的 coord(system const*, pos, ypr) 重载隐藏基类同名函数，显式走 system 的 setter
		static_cast<fantasy::system*>(node)->coord(xhqm::translate((float)i, (float)(i * 2), (float)(i * 3)));
		root->add_child_ptr(node);
		nodes.push_back(node);
	}

	auto t0 = std::chrono::steady_clock::now();
	fantasy::system::traverse(fantasy::matrix(4, 4), root, fantasy::matrix(4, 4));
	auto t1 = std::chrono::steady_clock::now();
	double tree_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

	//SoA 路径：position 三列连续存储，out 参数零构造读取 + 批量生成平移矩阵
	tuple_values<float, float, float> positions;
	for (xhqm::count i = 0; i < N; ++i)
	{
		positions.push((float)i, (float)(i * 2), (float)(i * 3));
	}

	auto t2 = std::chrono::steady_clock::now();
	std::vector<fantasy::matrix> instances;
	instances.reserve(N);
	for (xhqm::index i = 0; i < positions.size(); ++i)
	{
		float x, y, z;
		positions.get_value(i, x, y, z);
		instances.push_back(xhqm::translate(x, y, z));
	}
	auto t3 = std::chrono::steady_clock::now();
	double ecs_ms = std::chrono::duration<double, std::milli>(t3 - t2).count();

	std::cout << "[ecs_benchmark] N=" << N
		<< " tree_traverse=" << tree_ms << "ms"
		<< " soa_batch=" << ecs_ms << "ms" << std::endl;

	//结果一致：SoA 批量矩阵与树节点坐标矩阵逐实体相等
	CHECK(instances.size() == N);
	for (xhqm::index i = 0; i < N; i += 9973)   // 抽检验证
	{
		const fantasy::matrix& expect = static_cast<fantasy::system*>(nodes[i])->coord();
		CHECK_NEAR(instances[i](0, 3), expect(0, 3), 1e-4);
		CHECK_NEAR(instances[i](1, 3), expect(1, 3), 1e-4);
		CHECK_NEAR(instances[i](2, 3), expect(2, 3), 1e-4);
	}

	for (auto* node : nodes) { delete node; }
	delete root;
}
