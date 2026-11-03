#include "RhiViewport.h"
#include <chrono>   // 帧内耗时拆分（帧图执行 vs 起止帧+呈现）
#include <theory_render.h>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QResizeEvent>
#include <QKeyEvent>
#include <QEvent>
#include <QImage>
#include <cstdio>
#include <cmath>
#if defined(_WIN32)
#include <windows.h>
#endif

using namespace xhqm::fantasy;

// 当前线程累计 CPU 时间（毫秒）：用来判别「帧图执行是 CPU 忙算还是被 GPU 阻塞」。
// 墙钟 109ms 而 CPU 只 5ms ⇒ 在等 GPU/驱动；两者都 109ms ⇒ 主线程自己在算。
static double thread_cpu_ms()
{
#if defined(_WIN32)
	FILETIME c, e, k, u;
	if (!GetThreadTimes(GetCurrentThread(), &c, &e, &k, &u)) return 0.0;
	auto f = [](const FILETIME& t) { return (double)(((unsigned long long)t.dwHighDateTime << 32) | t.dwLowDateTime); };
	return (f(k) + f(u)) / 10000.0;   // 100ns 单位 → ms
#else
	return 0.0;
#endif
}

bool RhiViewport::event(QEvent* e)
{
	// 拦截 Close：关窗即退出，窗口销毁后帧循环若继续 makeCurrent/swapBuffers 会触发 Qt 断言崩溃（Trace/BPT）
	if (e->type() == QEvent::Close)
		QGuiApplication::quit();
	return QWindow::event(e);
}

RhiViewport::RhiViewport(Backend backend, QWindow* parent, QOpenGLContext* share)
	: QWindow(parent), m_backend(backend)
{
	if (m_backend == Backend::Vulkan)
	{
		// 最早选 Vulkan：确保后续 object 构造触发的 render_id()/init 走 Vulkan 后端
		render_backend_select(backend_kind::vulkan);
	}
	else
	{
		// OpenGL 后端：GL 单上下文多窗口 —— 引擎 GL 对象（纹理/VBO/程序/FBO）全挂在同一上下文上，
		// 附加窗口直接复用主窗口 context（makeCurrent 切到自己的 QSurface 渲染），不各自新建。
		setSurfaceType(QSurface::OpenGLSurface);   // 必需：否则 makeCurrent 报 non-opengl surface
		QSurfaceFormat fmt;
		fmt.setVersion(3, 3);
		fmt.setProfile(QSurfaceFormat::CoreProfile);
		fmt.setAlphaBufferSize(8);
		fmt.setDepthBufferSize(24);
		fmt.setStencilBufferSize(8);
		setFormat(fmt);
		if (share)
		{
			m_gl_ctx = share;   // 共享主窗口上下文（多窗口）
		}
		else
		{
			m_gl_ctx = new QOpenGLContext(this);
			m_gl_ctx->setFormat(fmt);
			m_gl_ctx->create();
		}
	}

	m_timer.setInterval(35);
	QObject::connect(&m_timer, &QTimer::timeout, [this]() { this->frame(); });
}

RhiViewport::~RhiViewport()
{
	m_timer.stop();
	// 先释放本窗口管线（其 pass 引用本窗口相机），再退 surface，最后按所有权删 world
	if (m_fg) { delete m_fg; m_fg = nullptr; }
	for (auto* p : m_passes) { if (p) delete p; }
	m_passes.clear();
	if (m_backend == Backend::Vulkan)
	{
		if (m_surface)
			render_backend_active().destroy_surface(reinterpret_cast<void*>(winId()));
	}
	else
	{
		if (m_gl_ctx) { m_gl_ctx->makeCurrent(this); m_gl_ctx->doneCurrent(); }
	}
	if (m_world && m_own_world) { delete m_world; m_world = nullptr; }
	// GL 上下文销毁前退役后端：进程静态 shader（inline static）在静态析构阶段
	// 仍会调 shader_delete，若 GL 上下文已销毁则无上下文崩溃；切 none 让删除变 no-op。
	xhqm::fantasy::render_backend_shutdown();
}

void RhiViewport::attach_world(xhqm::fantasy::world* w)
{
	m_world = w;
	m_own_world = false;
}

// Vulkan 多窗口：把本窗口设为活动 swapchain（同窗口同尺寸 = 纯激活；尺寸变化 = 重建）
bool RhiViewport::activate_surface()
{
	render_backend& rhi = render_backend_active();
	std::string err;
	uint w = (uint)width(), h = (uint)height();
	if (w == 0) w = 1; if (h == 0) h = 1;
	if (!rhi.create_surface(reinterpret_cast<void*>(winId()), w, h, err))
	{
		fprintf(stderr, "[FAIL] activate_surface(%p %ux%u): %s\n", (void*)winId(), w, h, err.c_str());
		return false;
	}
	return true;
}

// OpenGL 后端：确保自管上下文绑定本窗口并 current（引擎 GL 调用依赖当前线程的 GL 上下文）
bool RhiViewport::make_gl_current()
{
	if (!m_gl_ctx) return false;
	if (m_gl_ctx->makeCurrent(this)) return true;
	fprintf(stderr, "[GL] makeCurrent failed: ctx_valid=%d ctx_created=%d\n",
		m_gl_ctx->isValid() ? 1 : 0, m_gl_ctx->isValid() ? 1 : 0);
	this->create();   // 平台窗口可能尚未创建，补建后重试一次
	return m_gl_ctx->makeCurrent(this);
}

bool RhiViewport::initialize()
{
	using namespace xhqm::fantasy;

	if (m_backend == Backend::OpenGL)
	{
		// 必须先 current：object 构造触发的 render_id()/glewInit 需要现成的 GL 上下文
		if (!make_gl_current())
		{
			printf("[FAIL] GL context unavailable\n");
			return false;
		}
		render_backend_select(backend_kind::opengl);
	}
	else
	{
		// Vulkan：后端由构造函数 render_backend_select 选中，但惰性 init 仅在 active_ptr 为空时触发，
		// 此处显式初始化（否则 m_device 为空，create_surface 报 "Vulkan device not created"）
		std::string verr;
		if (!render_backend_active().init(verr))
		{
			printf("[FAIL] VK init: %s\n", verr.c_str());
			return false;
		}
	}

	// 1. 场景与相机：window0 创建并拥有 world；每窗口统一用 camera 挂 add_camera 多相机子树
	if (!m_world)
	{
		m_world = new xhqm::fantasy::world();
		m_own_world = true;
	}
	m_camera = new xhqm::fantasy::camera();
	m_world->add_camera(m_camera);   // 多相机子树（单位 coord，与默认相机不串联）

	// 2. 相机：透视投影 + 渐变背景（左手系默认）；orbit 姿态/焦点由相机自维护
	camera* cam = m_camera;
	cam->resize(width(), height(), 100);
	cam->ortho(false);   // 透视投影（固定 fov 45°）
	cam->orbit(true);                                    // 开启 orbit 自维护（life 每帧应用）
	cam->focus(xhqm::poit<value>(0, 0, -91.2));          // 轨道焦点：场景中心
	cam->ground({
		{ xhqm_rgba(150, 190, 220, 255), xhqm_rgba(150, 190, 220, 255) },
		{ xhqm_rgba(170, 205, 235, 255), xhqm_rgba(170, 205, 235, 255) },
		{ xhqm_rgba(190, 220, 245, 255), xhqm_rgba(190, 220, 245, 255) },
		{ xhqm_rgba(210, 235, 255, 255), xhqm_rgba(210, 235, 255, 255) },
		});

	// 3. 渲染管线：每窗口独立一条（多相机架构），fg->run() 手动驱动
	m_fg = setup_pipeline_for(cam, 0);
	m_passes = m_fg ? m_fg->passes() : std::vector<theory_pass*>();
	if (!m_fg)
	{
		printf("[FAIL] setup_pipeline_for\n");
		return false;
	}

	// 3.5 坐标系 gizmo：挂相机下（跟随相机，屏幕位置固定），theory_gizmo_sync 每帧同步姿态（三轴指向世界）
	if (m_gizmo)
	{
		auto* gizmo = new xhqm::fantasy::coord();
		for (auto* d : gizmo->handle_drawers()) d->shadow(false);   // UI 辅助器：不参与阴影
		m_camera->add_ui(gizmo);
		auto* gs = new xhqm::fantasy::theory_gizmo_sync(cam, gizmo);
		gs->fg(m_fg);
		m_fg->register_pass(gs);
		m_passes.push_back(gs);
	}

	// 4. 表面：Vulkan 每窗口一份 swapchain；OpenGL 直接使用窗口（引擎 GL 后端即时渲染）
	std::string err;
	render_backend& rhi = render_backend_active();
	if (m_backend == Backend::Vulkan)
	{
		if (!rhi.create_surface(reinterpret_cast<void*>(winId()), (uint)width(), (uint)height(), err))
		{
			printf("[FAIL] create_surface: %s\n", err.c_str());
			return false;
		}
		printf("[OK] backend=vulkan surface=%dx%d win=%p\n", width(), height(), (void*)winId());
	}
	else
	{
		if (!rhi.create_surface(nullptr, (uint)width(), (uint)height(), err))
		{
			printf("[FAIL] GL init: %s\n", err.c_str());
			return false;
		}
		printf("[OK] backend=opengl surface=%dx%d\n", width(), height());
	}
	m_surface = true;
	return true;
}

void RhiViewport::start()
{
	m_timer.start();
}

void RhiViewport::frame()
{
	if (!m_world || !m_surface || !m_fg) return;
	std::string err;
	render_backend& rhi = render_backend_active();

	if (m_backend == Backend::OpenGL)
	{
		// OpenGL 后端：自管上下文 current → 渲染（begin/end_frame 为 GL 空实现，保持对称）→ swap
		if (!make_gl_current()) return;
		rhi.begin_frame(err);
		m_fg->run();   // 仅驱动本窗口管线（多窗口各自独立驱动）
		rhi.end_frame();
		m_gl_ctx->swapBuffers(this);
		return;
	}

	// Vulkan 多窗口：先激活本窗口 swapchain，再驱动本窗口管线并 present
	if (!activate_surface()) return;
	const double t_a = std::chrono::duration<double, std::milli>(
		std::chrono::high_resolution_clock::now().time_since_epoch()).count();
	rhi.begin_frame(err);
	if (!err.empty())
	{
		// 表面尺寸过期（WM resize）：强制销毁重建（取表面当前实际尺寸），重试一次
		rhi.destroy_surface(reinterpret_cast<void*>(winId()));
		err.clear();
		if (activate_surface()) rhi.begin_frame(err);
	}
	const double t_b = std::chrono::duration<double, std::milli>(
		std::chrono::high_resolution_clock::now().time_since_epoch()).count();
	const double c_b = thread_cpu_ms();
	if (err.empty() && m_per_pass)
	{
		// 逐 pass：先标 use（等价 run() 的前半），再按注册序逐个 render() 并单独计时。
		// 不调 fg->life()：它内部会执行全部 callback，无法拆开。
		for (auto* p : m_fg->passes()) p->life();
		for (auto* p : m_fg->passes())
		{
			const double w0 = std::chrono::duration<double, std::milli>(
				std::chrono::high_resolution_clock::now().time_since_epoch()).count();
			const double p0 = thread_cpu_ms();
			p->render();
			const double p1 = thread_cpu_ms();
			const double w1 = std::chrono::duration<double, std::milli>(
				std::chrono::high_resolution_clock::now().time_since_epoch()).count();
			bool found = false;
			for (auto& e : m_pass_ms)
				if (e.first == p->pass_name()) { e.second.first += w1 - w0; e.second.second += p1 - p0; found = true; break; }
			if (!found) m_pass_ms.push_back({ p->pass_name(), { w1 - w0, p1 - p0 } });
		}
	}
	else if (err.empty()) m_fg->run();
	const double t_c = std::chrono::duration<double, std::milli>(
		std::chrono::high_resolution_clock::now().time_since_epoch()).count();
	const double c_c = thread_cpu_ms();
	m_ms_fg += t_c - t_b;
	m_ms_cpu_fg += c_c - c_b;
	// 单帧最大/超阈值计数：平均帧时间掩盖掉帧尖刺（描述符集环回卷、设备空闲、着色器编译等
	// 都是"每 N 帧一次"，看平均看不出来）。"一卡一卡"要的是这个量。
	// 前 20 帧单独记（首帧要建管线/描述符池/离屏，本来就是几十 ms 量级，混进来会盖住真的尖刺）。
	{
		const double fg = t_c - t_b;
		if (m_frames == 0) m_ms_fg_first = fg;
		else if (m_frames >= 20)
		{
			if (fg > m_ms_fg_max) m_ms_fg_max = fg;
			if (fg > 20.0) ++m_frames_slow;
		}
	}
	rhi.end_frame();
	// 帧内耗时拆分：帧图执行（shadow/scene/tonemap）与 起止帧+呈现。用来定位渲染慢在哪一半。
	// m_ms_cpu_fg 是同一段的「本线程 CPU 时间」：与墙钟接近=CPU 忙算，远小于墙钟=在等 GPU/驱动。
	m_ms_other += (t_b - t_a) + std::chrono::duration<double, std::milli>(
		std::chrono::high_resolution_clock::now().time_since_epoch()).count() - t_c;
	// 帧计数：让「窗口到底有没有在渲染」成为可程序化验证的量。
	// 实测教训：fly_mcns_view 曾漏调 viewport.start()，窗口一直是白的、只有鼠标点击才刷新一帧；
	// 因为所有验证都只看控制台输出，这个缺陷长期未被发现（快照走离屏路径不受影响，把它遮住了）。
	++m_frames;
}

double RhiViewport::pass_ms(const char* name) const
{
	for (const auto& e : m_pass_ms) if (e.first == name) return e.second.first;
	return 0.0;
}

double RhiViewport::pass_cpu_ms(const char* name) const
{
	for (const auto& e : m_pass_ms) if (e.first == name) return e.second.second;
	return 0.0;
}

std::vector<std::string> RhiViewport::pass_names() const
{
	std::vector<std::string> out;
	for (const auto& e : m_pass_ms) out.push_back(e.first);
	return out;
}

// 焦点平面每像素世界单位（相机自维护：正交取视深、透视取轨道距离）
float RhiViewport::world_per_pixel() const{
	if (!m_world) return 0.01f;
	return (float)m_camera->world_per_pixel();
}

void RhiViewport::mousePressEvent(QMouseEvent* e)
{
	m_last_pos = e->pos();
	m_left = (e->buttons() & Qt::LeftButton);
	m_right = (e->buttons() & Qt::RightButton);
	m_middle = (e->buttons() & Qt::MiddleButton);
}

void RhiViewport::mouseReleaseEvent(QMouseEvent* e)
{
	m_left = (e->buttons() & Qt::LeftButton);
	m_right = (e->buttons() & Qt::RightButton);
	m_middle = (e->buttons() & Qt::MiddleButton);
}

void RhiViewport::mouseMoveEvent(QMouseEvent* e)
{
	using namespace xhqm::fantasy;
	if (!m_world) return;
	const int dx = e->pos().x() - m_last_pos.x();
	const int dy = e->pos().y() - m_last_pos.y();

	if (m_left)
	{
		// 左键：平移（选中→对象，未选中→视口）
		m_camera->pan((float)dx, (float)dy);
	}
	else if (m_right)
	{
		// 右键：轨道旋转（业界 OrbitControls 语义）——相机绕焦点旋转，对象不动
		m_camera->orbit_add((float)dx * 0.008f, (float)-dy * 0.008f);
	}
	// 中键拖动不再绑定（滚轮为缩放）

	m_last_pos = e->pos();
}

void RhiViewport::mouseDoubleClickEvent(QMouseEvent* e)
{
	if (!m_world || !m_surface) return;
	using namespace xhqm::fantasy;
	object* obj = pick(e->pos().x(), e->pos().y());
	fprintf(stderr, "[RhiViewport] pick at (%d,%d) -> %s (id=%llu)\n",
		e->pos().x(), e->pos().y(), obj ? "hit" : "null", (unsigned long long)(obj ? obj->object_id() : 0));
	set_select(dynamic_cast<models*>(obj));
}

void RhiViewport::wheelEvent(QWheelEvent* e)
{
	using namespace xhqm::fantasy;
	if (!m_world || !m_camera) return;
	const bool zoom_in = e->angleDelta().y() > 0;
	// 滚轮缩放：未选中 dolly(改相机距离), 选中缩放对象
	const float s = zoom_in ? 1.10f : (1.0f / 1.10f);
	m_camera->zoom(s);
}

void RhiViewport::resizeEvent(QResizeEvent* e)
{
	// 多窗口：attach_world 与 initialize 之间可能收到几何事件，m_camera 尚未创建
	if (!m_world || !m_camera) return;
	m_camera->resize(e->size().width(), e->size().height(), 100);
	if (m_backend != Backend::Vulkan || !m_surface) return;
	// 重建本窗口 Vulkan swapchain（同步后销毁再创建；其他窗口状态不动）
	render_backend& rhi = render_backend_active();
	rhi.finish();
	rhi.destroy_surface(reinterpret_cast<void*>(winId()));
	std::string err;
	rhi.create_surface(reinterpret_cast<void*>(winId()), (uint)e->size().width(), (uint)e->size().height(), err);
}

void RhiViewport::keyPressEvent(QKeyEvent* e)
{
	const int key = e->key();
	if (m_camera)
	{
		if (key == Qt::Key_Up) { m_camera->key_forward(true); return; }
		if (key == Qt::Key_Down) { m_camera->key_back(true); return; }
		if (key == Qt::Key_Left) { m_camera->key_left(true); return; }
		if (key == Qt::Key_Right) { m_camera->key_right(true); return; }
		if (key == Qt::Key_W) { m_camera->key_model_forward(true); return; }
		if (key == Qt::Key_S) { m_camera->key_model_back(true); return; }
		if (key == Qt::Key_A) { m_camera->key_model_left(true); return; }
		if (key == Qt::Key_D) { m_camera->key_model_right(true); return; }
	}
	if (key_handler) key_handler(key);
}

void RhiViewport::keyReleaseEvent(QKeyEvent* e)
{
	const int key = e->key();
	if (!m_camera) return;
	if (key == Qt::Key_Up) m_camera->key_forward(false);
	else if (key == Qt::Key_Down) m_camera->key_back(false);
	else if (key == Qt::Key_Left) m_camera->key_left(false);
	else if (key == Qt::Key_Right) m_camera->key_right(false);
	else if (key == Qt::Key_W) m_camera->key_model_forward(false);
	else if (key == Qt::Key_S) m_camera->key_model_back(false);
	else if (key == Qt::Key_A) m_camera->key_model_left(false);
	else if (key == Qt::Key_D) m_camera->key_model_right(false);
}

xhqm::fantasy::object* RhiViewport::pick(int x, int y)
{
	using namespace xhqm::fantasy;
	if (!m_world || !m_surface) return nullptr;

	camera* cam = m_camera;

	// OpenGL 后端：相机内建 id 拾取（需要 current 上下文）
	if (m_backend == Backend::OpenGL)
	{
		if (!make_gl_current()) return nullptr;
		auto vs = cam->view_size();
		int vh = (int)(vs[1] ? vs[1] : height());
		int gl_y = vh - y - 1;   // 窗口坐标 → 左下原点
		return cam->slected(x, gl_y);
	}

	// Vulkan 后端：离屏 id 渲染读回
	render_backend& rhi = render_backend_active();
	if (!activate_surface()) return nullptr;
	object* root = cam->root_ptr();

	// 懒创建/重建拾取离屏（尺寸必须与相机 view_size 一致，不能用窗口外框尺寸）
	auto vs = cam->view_size();
	uint vw = vs[0], vh = vs[1];
	if (vw == 0) vw = (uint)width();
	if (vh == 0) vh = (uint)height();
	if (vw == 0) vw = 1; if (vh == 0) vh = 1;
	if (m_pick_target == 0 || m_pick_w != vw || m_pick_h != vh)
	{
		if (m_pick_target) rhi.delete_offscreen(m_pick_target);
		std::string err;
		m_pick_target = rhi.create_offscreen(vw, vh, 1, err);
		m_pick_w = vw; m_pick_h = vh;
	}
	if (!m_pick_target) return nullptr;

	std::string err;
	rhi.begin_frame(err);
	{
		matrix window = cam->window();
		matrix cmat = system::transform(root, cam);

		// 1) 离屏 id 渲染（不碰 swapchain）
		rhi.begin_offscreen(m_pick_target);
		cam->clear(xhqm_rgba(0, 0, 0, 0));
		system::traverse(window * cmat, root, matrix(4, 4));
		models::idcard(root);
		rhi.end_offscreen();

		// 2) 重新渲染正常场景到 swapchain（present 的是场景，避免 id 画面闪屏）
		if (m_fg) m_fg->run();
	}
	rhi.end_frame();

	// 读离屏（窗口坐标 -> GL 左下角坐标；用相机 view 高度，与渲染一致）
	uint object_id = 0;
	int gl_y = (int)vh - y - 1;
	rhi.read_offscreen(m_pick_target, x, gl_y, &object_id);

	for (auto* obj : (*m_world))
	{
		if (obj->object_id() == object_id) return obj;
	}
	return nullptr;
}

void RhiViewport::set_select(xhqm::fantasy::models* m)
{
	using namespace xhqm::fantasy;
	if (m_select)
	{
		for (auto* obj : (*m_select))
		{
			models* mm = dynamic_cast<models*>(obj);
			if (mm) mm->drawer_select(false);
		}
	}
	m_select = m;
	if (m_camera) m_camera->operand(m_select);   // 同步到相机操作对象（缩放/平移作用目标）
	if (m_select)
	{
		for (auto* obj : (*m_select))
		{
			models* mm = dynamic_cast<models*>(obj);
			if (mm) mm->drawer_select(true);
		}
	}
}

bool RhiViewport::dump_frame_raw(const std::string& path)
{
	using namespace xhqm::fantasy;
	if (!m_world || !m_surface) return false;
	if (m_backend == Backend::OpenGL && !make_gl_current()) return false;
	render_backend& rhi = render_backend_active();
	if (m_backend == Backend::Vulkan && !activate_surface()) return false;
	uint w = (uint)width(), h = (uint)height();
	if (w == 0) w = 1; if (h == 0) h = 1;

	std::vector<uint> px((size_t)w * h);
	rhi.read_pixels(0, 0, (sint)w, (sint)h, px.data());

	// BGR→RGB 存 PNG（与 snapshot_to 相同换算）
	std::vector<unsigned char> bytes((size_t)w * h * 4);
	for (size_t i = 0; i < px.size(); ++i)
	{
		uint p = px[i];
		bytes[i * 4 + 0] = (p >> 16) & 0xFF;
		bytes[i * 4 + 1] = (p >> 8) & 0xFF;
		bytes[i * 4 + 2] = p & 0xFF;
		bytes[i * 4 + 3] = (p >> 24) & 0xFF;
	}
	QImage img(bytes.data(), (int)w, (int)h, (int)w * 4, QImage::Format_RGBA8888);
	img = img.copy();
	return img.save(QString::fromUtf8(path.c_str()));
}

int RhiViewport::watch_orange_passive()
{
	using namespace xhqm::fantasy;
	if (!m_world || !m_surface) return -1;
	if (m_backend == Backend::OpenGL && !make_gl_current()) return -1;
	render_backend& rhi = render_backend_active();
	if (m_backend == Backend::Vulkan && !activate_surface()) return -1;
	uint w = (uint)width(), h = (uint)height();
	if (w == 0) w = 1; if (h == 0) h = 1;

	std::vector<uint> px((size_t)w * h);
	rhi.read_pixels(0, 0, (sint)w, (sint)h, px.data());

	long orange = 0, blue = 0, low_alpha = 0;
	uint min_a = 255;
	for (uint y = 0; y < h / 4; ++y)
	{
		const uint* row = &px[(size_t)y * w];
		for (uint x = 0; x < w; ++x)
		{
			uint p = row[x];
			uint r = p & 0xFF, g = (p >> 8) & 0xFF, b = (p >> 16) & 0xFF, a = (p >> 24) & 0xFF;
			if (a < min_a) min_a = a;
			if (a < 250) ++low_alpha;
			if (r > 150 && g > 110 && g < 185 && b < 90) ++orange;
			else if (b > 150 && r < 200 && g > 140) ++blue;
		}
	}
	if (orange > 1000 || low_alpha > 0)
	{
		fprintf(stderr, "[WATCHP] orange=%ld blue=%ld alpha_min=%u alpha_low=%ld (FLASH IN IMAGE)\n", orange, blue, min_a, low_alpha);
		return orange;
	}
	return 0;
}

int RhiViewport::watch_orange()
{
	using namespace xhqm::fantasy;
	if (!m_world || !m_surface) return -1;
	render_backend& rhi = render_backend_active();
	uint w = (uint)width(), h = (uint)height();
	if (w == 0) w = 1; if (h == 0) h = 1;

	frame();

	std::vector<uint> px((size_t)w * h);
	rhi.read_pixels(0, 0, (sint)w, (sint)h, px.data());

	// 天空区（上 25%）：统计「橙色系」像素（R>150, G 110-180, B<90，覆盖 #BA8F2F 与角色色）+ alpha 异常
	long orange = 0, blue = 0, low_alpha = 0;
	uint min_a = 255;
	for (uint y = 0; y < h / 4; ++y)
	{
		const uint* row = &px[(size_t)y * w];
		for (uint x = 0; x < w; ++x)
		{
			uint p = row[x];
			uint r = p & 0xFF, g = (p >> 8) & 0xFF, b = (p >> 16) & 0xFF, a = (p >> 24) & 0xFF;
			if (a < min_a) min_a = a;
			if (a < 250) ++low_alpha;
			if (r > 150 && g > 110 && g < 185 && b < 90) ++orange;
			else if (b > 150 && r < 200 && g > 140) ++blue;
		}
	}
	if (orange > 1000 || low_alpha > 0)
	{
		fprintf(stderr, "[WATCH] orange=%ld blue=%ld alpha_min=%u alpha_low=%ld (FLASH IN IMAGE)\n", orange, blue, min_a, low_alpha);
		return orange;
	}
	return 0;
}

bool RhiViewport::snapshot_to(const std::string& path)
{
	using namespace xhqm::fantasy;
	if (!m_world || !m_surface) return false;
	render_backend& rhi = render_backend_active();
	uint w = (uint)width(), h = (uint)height();
	if (w == 0) w = 1; if (h == 0) h = 1;

	if (m_backend == Backend::OpenGL)
	{
		// GL：先渲染到后缓冲 → 读回 → 再交换。避免「交换后读回旧缓冲」读到上一帧/他窗口内容。
		if (!make_gl_current()) return false;
		std::string err;
		rhi.begin_frame(err);
		if (m_fg) m_fg->run();
		rhi.end_frame();
		std::vector<uint> px((size_t)w * h);
		rhi.read_pixels(0, 0, (sint)w, (sint)h, px.data());
		m_gl_ctx->swapBuffers(this);

		std::vector<unsigned char> bytes((size_t)w * h * 4);
		for (size_t i = 0; i < px.size(); ++i)
		{
			uint p = px[i];
			bytes[i * 4 + 0] = (p >> 16) & 0xFF;
			bytes[i * 4 + 1] = (p >> 8) & 0xFF;
			bytes[i * 4 + 2] = p & 0xFF;
			bytes[i * 4 + 3] = (p >> 24) & 0xFF;
		}
		QImage img(bytes.data(), (int)w, (int)h, (int)w * 4, QImage::Format_RGBA8888);
		img = img.copy();
		bool ok = img.save(QString::fromUtf8(path.c_str()));
		fprintf(stderr, "[snap] rhi saved %s ok=%d (%ux%u) drawcalls=%u\n", path.c_str(), ok ? 1 : 0, w, h, rhi.draw_call_count());
		return ok;
	}

	// Vulkan：渲染一整帧（present 后 read_pixels 读回本窗口 swapchain 当前图）
	frame();

	std::vector<uint> px((size_t)w * h);
	rhi.read_pixels(0, 0, (sint)w, (sint)h, px.data());

	// read_pixels 按 uint 返回（SWAPCHAIN 常为 B8G8R8A8，最低字节 = B），交换 R/B 成 RGBA
	std::vector<unsigned char> bytes((size_t)w * h * 4);
	for (size_t i = 0; i < px.size(); ++i)
	{
		uint p = px[i];
		bytes[i * 4 + 0] = (p >> 16) & 0xFF;   // R
		bytes[i * 4 + 1] = (p >> 8) & 0xFF;    // G
		bytes[i * 4 + 2] = p & 0xFF;           // B
		bytes[i * 4 + 3] = (p >> 24) & 0xFF;   // A
	}
	QImage img(bytes.data(), (int)w, (int)h, (int)w * 4, QImage::Format_RGBA8888);
	img = img.copy();
	bool ok = img.save(QString::fromUtf8(path.c_str()));
	fprintf(stderr, "[snap] rhi saved %s ok=%d (%ux%u) drawcalls=%u\n", path.c_str(), ok ? 1 : 0, w, h, rhi.draw_call_count());
	return ok;
}

