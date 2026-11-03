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

	// 场景访问
	xhqm::fantasy::world* world_ptr() const { return m_world; }
	xhqm::fantasy::camera* camera_ptr() const { return m_camera; }
	xhqm::fantasy::theory_frame_graph* frame_graph_ptr() const { return m_fg; }

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
	bool m_surface = false;
	QTimer m_timer;
	QOpenGLContext* m_gl_ctx = nullptr;   // OpenGL 后端自管上下文
	unsigned int m_pick_target = 0;   // Vulkan 拾取离屏目标（懒创建）
	unsigned int m_pick_w = 0, m_pick_h = 0;

	QPoint m_last_pos;
	bool m_left = false;
	bool m_right = false;
	bool m_middle = false;
};

#endif
