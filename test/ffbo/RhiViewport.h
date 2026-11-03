#pragma once
#ifndef XHQM_RHI_VIEWPORT
#define XHQM_RHI_VIEWPORT

// 统一 3D 查看视口（QWindow 对称壳）：Vulkan 后端由引擎自建 surface（多窗口每窗口一份 swapchain），
// OpenGL 后端由 QOpenGLContext 自管上下文（makeCurrent 帧循环 + swapBuffers）。
// 交互 / 选中 / 相机 orbit / 场景构建与后端无关，全部共享。
// 多相机架构：每个窗口 = 独立 camera + 独立 theory_frame_graph 管线（setup_pipeline_for），
// 可共享同一 world 查看同一场景（attach_world）。
#include <xhqm/fantasy>
#include <xhqm/algebra>
#include <rhi_no.h>
#include <theory_pass.h>
#include <camera.h>
#include <QWindow>
#include <QTimer>
#include <QPoint>
#include <QEvent>
#include <QOpenGLContext>
#include <functional>
#include <string>
#include <utility>
#include <vector>

class RhiViewport : public QWindow
{
public:
	// 渲染后端：由启动参数 --backend 决定（auto 探测 Vulkan，失败回退 OpenGL）
	enum class Backend { Vulkan, OpenGL };
	// share：OpenGL 多窗口共享上下文（附加窗口须共享主窗口 context，才能见同一批 GL 对象）
	explicit RhiViewport(Backend backend, QWindow* parent = nullptr, QOpenGLContext* share = nullptr);
	~RhiViewport() override;

	Backend backend() const { return m_backend; }
	QOpenGLContext* gl_context() const { return m_gl_ctx; }

	// 场景与 RHI 初始化（需在窗口 show + winId 之后调用；GL 分支自行建立当前上下文）
	bool initialize();

	// 多窗口共享场景：initialize 之前注入外部 world（本窗口不再创建/拥有 world，
	// 改为在 world 上挂一颗附加相机 + 独立管线，与其他窗口看同一场景）
	void attach_world(xhqm::fantasy::world* w);

	// 启动帧循环
	void start();

	// 已渲染帧数（诊断：用来程序化确认帧循环确实在跑 —— 见 RhiViewport.cpp 里的说明）
	unsigned long long frame_count() const { return m_frames; }
	// 帧内耗时拆分（毫秒，累计）：fg = 帧图执行（含 shadow/scene/tonemap），other = 起止帧+呈现
	double frame_ms_fg() const { return m_ms_fg; }
	double frame_ms_other() const { return m_ms_other; }
	// 帧图执行段的「本线程 CPU 时间」累计（毫秒）：判别 CPU 忙算 vs 阻塞等 GPU
	double frame_ms_cpu_fg() const { return m_ms_cpu_fg; }
	// 单帧最大帧图执行时间与超阈值帧数：看掉帧尖刺（平均帧时间会把它抹平）
	double frame_ms_fg_max() const { return m_ms_fg_max; }
	double frame_ms_fg_first() const { return m_ms_fg_first; }
	unsigned long long frames_slow() const { return m_frames_slow; }

	// 逐 pass 计时（--perf-pass）：绕开 fg->run() 的拓扑执行，按注册序直接调各 pass->render()，
	// 单独统计每个 pass 的墙钟/CPU。本管线不声明资源（declare 都是空 reads/writes），
	// 所以按注册序渲染与 fg 的拓扑序等价。
	void per_pass(bool v) { m_per_pass = v; }
	double pass_ms(const char* name) const;
	double pass_cpu_ms(const char* name) const;
	std::vector<std::string> pass_names() const;

	// 场景访问
	xhqm::fantasy::world* world_ptr() const { return m_world; }
	xhqm::fantasy::camera* camera_ptr() const { return m_camera; }
	xhqm::fantasy::theory_frame_graph* frame_graph_ptr() const { return m_fg; }

	// 是否创建相机坐标 gizmo（initialize 之前设置才生效）。
	// 实测：coord gizmo 的 4 个 drawer 会发出 199 次 draw/帧（ploys 每个 topol 一次 rhi.draw），
	// 而引擎每次 draw 固定 0.196ms ⇒ 光 gizmo 就吃 39ms/pass、两个 pass 78ms/帧。
	// 纯仿真视口不需要它，默认关掉；要看坐标轴再打开。
	void gizmo(bool v) { m_gizmo = v; }

	// 点选拾取（x/y 为窗口坐标）：返回命中的 object
	xhqm::fantasy::object* pick(int x, int y);

	// 选中/取消选中
	void set_select(xhqm::fantasy::models* m);

	// 渲染场景到颜色离屏并导出 PNG（无头读回，不受窗口可见性影响）
	bool snapshot_to(const std::string& path);

	// 诊断：渲染一帧后读回 swapchain，统计天空区橙色像素（检测闪帧是否在图像内）
	int watch_orange();

	// 诊断：不重绘，直接读回当前 swapchain 图（高速采样捕捉闪帧）
	int watch_orange_passive();

	// 诊断：读回当前 swapchain 图并原样存盘（含 BGR 原始数据，供帧级对比）
	bool dump_frame_raw(const std::string& path);

	// 按键回调（main 设置：动画切换 / gizmo 显隐）
	std::function<void(int)> key_handler;

protected:
	void mousePressEvent(QMouseEvent* e) override;
	void mouseReleaseEvent(QMouseEvent* e) override;
	void mouseMoveEvent(QMouseEvent* e) override;
	void mouseDoubleClickEvent(QMouseEvent* e) override;
	void wheelEvent(QWheelEvent* e) override;
	void resizeEvent(QResizeEvent* e) override;
	void keyPressEvent(QKeyEvent* e) override;
	void keyReleaseEvent(QKeyEvent* e) override;
	bool event(QEvent* e) override;   // 拦截 Close：关窗即退出，避免窗口销毁后帧循环继续 swap 导致崩溃

private:
	void frame();   // 激活本窗口表面 → begin_frame → 本窗口管线 fg->run() → end_frame（GL 分支外加 makeCurrent/swap）
	float world_per_pixel() const;   // 焦点平面每像素世界单位（平移 1:1 贴手）
	bool make_gl_current();          // OpenGL 后端：绑定并 current 自管上下文
	bool activate_surface();         // Vulkan 多窗口：把本窗口设为活动 swapchain

	Backend m_backend;
	xhqm::fantasy::world* m_world = nullptr;
	bool m_own_world = true;                     // 是否由本窗口创建并拥有 world
	xhqm::fantasy::camera* m_camera = nullptr;   // 本窗口相机（camera，统一挂 add_camera 多相机子树）
	xhqm::fantasy::theory_frame_graph* m_fg = nullptr;   // 本窗口独立渲染管线（standalone，fg->run() 驱动）
	std::vector<xhqm::fantasy::theory_pass*> m_passes;   // 本窗口管线 pass（析构释放）
	xhqm::fantasy::models* m_select = nullptr;
	bool m_gizmo = true;                         // 是否创建相机坐标 gizmo（gizmo(false) 关闭）
	bool m_surface = false;
	QTimer m_timer;
	QOpenGLContext* m_gl_ctx = nullptr;   // OpenGL 后端自管上下文
	unsigned int m_pick_target = 0;   // Vulkan 拾取离屏目标（懒创建）
	unsigned int m_pick_w = 0, m_pick_h = 0;

	QPoint m_last_pos;
	bool m_left = false;
	bool m_right = false;
	bool m_middle = false;
	unsigned long long m_frames = 0;   // 已渲染帧数（诊断用）
	double m_ms_fg = 0.0, m_ms_other = 0.0;   // 帧内耗时累计（毫秒）：帧图执行 / 起止帧+呈现
	double m_ms_cpu_fg = 0.0;                  // 帧图执行段的本线程 CPU 时间累计（毫秒）
	double m_ms_fg_max = 0.0;                  // 第 20 帧之后的单帧帧图执行最大值（毫秒）：掉帧尖刺
	double m_ms_fg_first = 0.0;                // 首帧耗时（含建管线/描述符池/离屏，单独记）
	unsigned long long m_frames_slow = 0;      // 第 20 帧之后帧图执行 > 20ms 的帧数
	bool m_per_pass = false;                   // 逐 pass 计时模式
	std::vector<std::pair<std::string, std::pair<double, double>>> m_pass_ms;   // 名字 → (墙钟, CPU)
};

#endif
