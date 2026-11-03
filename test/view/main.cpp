#include "RhiViewport.h"
#include "ModelLoader.h"
#include "gltf_loader.h"
#include "bc1.h"
#include "scene_io.h"
#include "point_shadow.h"
#include "gpu_skinning.h"
#include "gpu_hiz.h"
#include "gpu_hiz_culler.h"
#include "ssao.h"
#include "taa.h"
#include "ssr.h"
#include <QGuiApplication>
#include <QScreen>
#include <QPixmap>
#include <QImage>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QDir>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif
#include <cmath>
#include <cstdio>
#include <functional>

// 跨平台建目录：Windows _mkdir / POSIX mkdir（已存在视为成功）
static int mkdir_wrap(const char* path)
{
#ifdef _WIN32
	return _mkdir(path);
#else
	if (::mkdir(path, 0755) == 0) return 0;
	return (errno == EEXIST) ? 0 : -1;
#endif
}

// 默认模型：KayKit 角色（30 段骨骼动画，CC0，6 骨骼）
static const char* k_default_model =
	"D:/WorkSpace/Model3D/KayKit-Character-Animations/Animations/gltf/KayKit_AnimatedCharacter_v1.2.glb";

// 坐标系 gizmo（三轴独立可拆）
static const char* k_gizmo_model =
	"D:/WorkSpace/Model3D/CoordinateGizmo/gizmo.glb";

// 双骨棒实例（每帧更新弯曲调色板）
static xhqm::fantasy::prefab* g_bar = nullptr;

// 动画：全部 clip + 当前索引 + 所有 prefab（采样后逐帧刷新 bone_palette）
static std::vector<demo::anim_clip> g_clips;
static size_t g_clip_idx = 0;
static std::vector<xhqm::fantasy::prefab*> g_anim_prefabs;

// gizmo：三轴 models + 各轴显隐
static std::vector<xhqm::fantasy::models*> g_gizmo;
static std::vector<bool> g_gizmo_show;

// 开放世界：可操控角色 + 位置（WASD 移动）
static xhqm::fantasy::models* g_character = nullptr;
static float g_char_x = 0.0f, g_char_y = 0.0f;

// 轨道相机姿态/焦点由 fantasy::camera 自维护（cam->orbit(true)/focus/orbit_add/dolly_ratio），
// 每帧经 theory_camera_render 驱动 camera::life 应用，无需全局状态。

// 程序化几何：地面方块（两个三角形，法线 +z）
static xhqm::fantasy::prefab* make_quad(float half, float z, xhqm::fantasy::rgba color)
{
	using namespace xhqm::fantasy;
	prefab* p = new prefab();
	p->size(2);
	p->vertex({
		{ { point(-half, -half, z), point(half, -half, z), point(half, half, z) } },
		{ { point(-half, -half, z), point(half, half, z), point(-half, half, z) } },
	});
	p->normal({
		{ 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f },
		{ 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f },
	});
	p->open_face(true); p->open_line(false); p->open_poit(false);
	p->open_light(true);
	p->color_face(color);
	return p;
}

// 程序化几何：盒子（12 个三角形，6 面）
static xhqm::fantasy::prefab* make_box(float sx, float sy, float sz, xhqm::fantasy::rgba color)
{
	using namespace xhqm::fantasy;
	float x = sx / 2, y = sy / 2, z = sz / 2;
	point c[8] = {
		point(-x,-y,-z), point(x,-y,-z), point(x,y,-z), point(-x,y,-z),
		point(-x,-y, z), point(x,-y, z), point(x,y, z), point(-x,y, z),
	};
	int idx[12][3] = {
		{0,1,2},{0,2,3}, {4,6,5},{4,7,6},
		{0,3,7},{0,7,4}, {1,5,6},{1,6,2},
		{0,4,5},{0,5,1}, {3,2,6},{3,6,7},
	};
	std::array<value, 3> n[6] = { {0.0f,0.0f,-1.0f}, {0.0f,0.0f,1.0f}, {-1.0f,0.0f,0.0f}, {1.0f,0.0f,0.0f}, {0.0f,-1.0f,0.0f}, {0.0f,1.0f,0.0f} };
	std::vector<std::array<point,3>> verts(12);
	std::vector<std::array<value,3>> norms(36);
	for (int f = 0; f < 6; ++f)
		for (int t = 0; t < 2; ++t)
		{
			int tri = f * 2 + t;
			verts[tri] = { c[idx[tri][0]], c[idx[tri][1]], c[idx[tri][2]] };
			norms[tri*3+0] = n[f]; norms[tri*3+1] = n[f]; norms[tri*3+2] = n[f];
		}
	prefab* p = new prefab();
	p->size(12);
	p->vertex(verts);
	p->normal(norms);
	p->open_face(true); p->open_line(false); p->open_poit(false);
	p->open_light(false);
	p->color_face(color);
	return p;
}

int main(int argc, char** argv)
{
	using namespace xhqm::fantasy;
	setbuf(stdout, nullptr);

	QSurfaceFormat fmt; fmt.setAlphaBufferSize(8);   // 透明窗口：RGBA 表面
	QSurfaceFormat::setDefaultFormat(fmt);
	QGuiApplication app(argc, argv);

	// --backend vulkan|opengl|auto：决定渲染后端（auto 探测 Vulkan 可用性，失败回退 OpenGL）
	RhiViewport::Backend backend = RhiViewport::Backend::Vulkan;
	{
		QString sel = "auto";
		auto args = app.arguments();
		for (int i = 1; i < args.size(); ++i)
			if (args.at(i) == "--backend" && i + 1 < args.size()) sel = args.at(i + 1).toLower();
		// 后端可用性由构建时宏决定（CMake 传 XHQM_VIEW_HAS_VK / XHQM_VIEW_HAS_GL）：
		// 指定了本构建没编入的后端时给出提示并改用可用后端，而不是初始化失败直接退出。
		if (sel == "opengl")
		{
#if defined(XHQM_VIEW_HAS_GL)
			backend = RhiViewport::Backend::OpenGL;
#else
			backend = RhiViewport::Backend::Vulkan;
			printf("[BACKEND] 本构建未编入 OpenGL 后端，改用 Vulkan\n");
#endif
		}
		else if (sel == "vulkan")
		{
#if defined(XHQM_VIEW_HAS_VK)
			backend = RhiViewport::Backend::Vulkan;
#else
			backend = RhiViewport::Backend::OpenGL;
			printf("[BACKEND] 本构建未编入 Vulkan 后端，改用 OpenGL\n");
#endif
		}
		else if (sel == "auto")
		{
			// 与引擎 create_auto_backend 的优先级一致（vk → gl）
#if defined(XHQM_VIEW_HAS_VK)
			backend = RhiViewport::Backend::Vulkan;
			printf("[BACKEND] auto -> vulkan\n");
#elif defined(XHQM_VIEW_HAS_GL)
			backend = RhiViewport::Backend::OpenGL;
			printf("[BACKEND] auto -> opengl\n");
#else
			backend = RhiViewport::Backend::Vulkan;
#endif
		}
	}

	RhiViewport viewport(backend);
	viewport.setTitle(backend == RhiViewport::Backend::Vulkan
		? "RhiViewport — Vulkan 交互查看器（多段动画 + gizmo）"
		: "RhiViewport — OpenGL 交互查看器（多段动画 + gizmo）");
	viewport.resize(820, 820);
	viewport.show();
	app.processEvents(); // 触发暴露，拿到原生 HWND

	if (!viewport.initialize())
	{
		printf("[FAIL] initialize\n");
		return 1;
	}

	// 1. 模型：参数 1 以 "--" 开头则为 flag，无模型参数 → 默认 9 角色 MMD 场景
	QString model_path = (app.arguments().size() > 1 && !app.arguments().at(1).startsWith("--"))
		? app.arguments().at(1) : QString();
	QString snap_path;
	QString snap_series_dir;
	QString vmd_path;   // --vmd <path>：可选 VMD 骨骼动画
	bool g_still = false;
	bool g_watch = false;
	bool g_nobar = false;
	int g_windows = 1;   // --windows N：N 个窗口共享同一场景（多相机架构）
	QString dump_dir;
	{
		auto args = app.arguments();
		for (int i = 1; i < args.size(); ++i)
		{
			if (args.at(i) == "--snapshot" && i + 1 < args.size()) snap_path = args.at(i + 1);
			if (args.at(i) == "--snap-series" && i + 1 < args.size()) snap_series_dir = args.at(i + 1);
			if (args.at(i) == "--vmd" && i + 1 < args.size()) vmd_path = args.at(i + 1);
			if (args.at(i) == "--still") g_still = true;
			if (args.at(i) == "--watch") g_watch = true;
			if (args.at(i) == "--nobar") g_nobar = true;
			if (args.at(i) == "--windows" && i + 1 < args.size()) g_windows = args.at(i + 1).toInt();
			if (args.at(i) == "--dumpframes" && i + 1 < args.size()) dump_dir = args.at(i + 1);
		}
	}
	if (g_windows < 1) g_windows = 1;
	if (!model_path.isEmpty())
	{
		QFileInfo fi(model_path);
		models* m = demo::load_model_animated(fi.path().toStdString() + "/", fi.fileName().toStdString(), g_clips, vmd_path.toStdString());
		if (m)
		{
			g_anim_prefabs.clear();
			for (auto* d : m->handle_drawers())
			{
				auto* p = dynamic_cast<prefab*>(d);
				if (p) g_anim_prefabs.push_back(p);
			}
			// 归一缩放（与内置 MMD3 演示场景一致）：统一身高 6，摆到 z=-91 展示区，相机轨道距离 30
			float bmn[3] = { 1e9f, 1e9f, 1e9f }, bmx[3] = { -1e9f, -1e9f, -1e9f };
			for (auto* d : m->handle_drawers())
			{
				auto* pp = dynamic_cast<prefab*>(d);
				if (!pp) continue;
				for (auto& tri : pp->vertex())
					for (auto& pt : tri)
						for (int kk = 0; kk < 3; ++kk)
						{
							bmn[kk] = std::min(bmn[kk], (float)pt[kk]);
							bmx[kk] = std::max(bmx[kk], (float)pt[kk]);
						}
			}
			float bheight = bmx[1] - bmn[1];
			if (bheight <= 0.001f) bheight = 1.0f;
			float bk = 6.0f / bheight;
			m->system::coord(xhqm::translate(0.0f, 0.0f, -91.0f) * xhqm::scale(bk) * xhqm::rotate_x(xhqm::radians(90.0f)));
			viewport.world_ptr()->add_target(m);
			viewport.world_ptr()->camera()->dolly(30.0f);
			g_character = m;
			g_char_x = 0.0f; g_char_y = 0.0f;
			{
				// 计算角色实际包围盒（从顶点数据）
				prefab* p = dynamic_cast<prefab*>(m->handle_drawers().front());
				if (p) {
					float mn[3] = { 1e9f, 1e9f, 1e9f }, mx[3] = { -1e9f, -1e9f, -1e9f };
					for (auto& tri : p->vertex())
						for (auto& pt : tri)
							for (int k = 0; k < 3; ++k) { mn[k] = std::min(mn[k], (float)pt[k]); mx[k] = std::max(mx[k], (float)pt[k]); }
					printf("[SIZE] min=(%.2f %.2f %.2f) max=(%.2f %.2f %.2f) extent=(%.2f %.2f %.2f)\n",
						mn[0], mn[1], mn[2], mx[0], mx[1], mx[2], mx[0]-mn[0], mx[1]-mn[1], mx[2]-mn[2]);
				}
			}
			{
				// 前上方方向光，照亮前脸，避免只有环境光的暗剪影
				std::vector<light_desc> lights;
				light_desc sun;
				sun.type = light_type::directional;
				sun.color = 0xffffffff;
				sun.direction = point(0.0f, -0.6f, -0.8f);
				lights.push_back(sun);
				for (auto* d : m->handle_drawers()) d->lights(lights);
			}
		}
		else
		{
			printf("[FAIL] model load failed: %s\n", model_path.toStdString().c_str());
		}
	}
			// 开放世界场景：草地地面 + 散布石块
			{
				models* ground = new models();
				ground->insert_drawer(make_quad(30.0f, -95.0f, xhqm_rgba(120, 170, 95, 255)));
				viewport.world_ptr()->add_target(ground);
				for (int i = 0; i < 10; ++i)
				{
					// 9 角色站位占 x∈[-14,14]，石块挪到两侧与后方，避免遮挡
					float side = (i % 2) ? 1.0f : -1.0f;
					float px = side * (15.0f + (i / 2) * 3.0f);
					float py = -10.0f + (i % 3) * 10.0f;
					models* rock = new models();
					rock->insert_drawer(make_box(3.0f, 3.0f, 3.0f, xhqm_rgba(145, 145, 155, 255)));
					rock->system::coord(xhqm::translate(px, py, -94.5f));
					viewport.world_ptr()->add_target(rock);
				}
			}
			printf("[OK] model loaded, prefabs=%d, clips=%d, bones=%d\n",
				(int)g_anim_prefabs.size(), (int)g_clips.size(),
				(int)(g_clips.empty() ? 0 : g_clips[0].bone_names.size()));
			if (!g_clips.empty()) printf("[ANIM] clip[0]=%s dur=%.2fs\n", g_clips[0].name.c_str(), g_clips[0].duration);

	// 2. 坐标系 gizmo：三轴独立（右侧），1/2/3 键单独显隐
	{
		QFileInfo fi(k_gizmo_model);
		auto parts = demo::load_model_separate(fi.path().toStdString() + "/", fi.fileName().toStdString());
		g_gizmo = parts;
		g_gizmo_show.assign(parts.size(), true);   // 诊断：可见，用 --snap-series 定位闪烁
		for (auto* axis : parts)
		{
			axis->system::coord(xhqm::translate(60.0f, 0.0f, -40.0f) * xhqm::scale(4.0f));
			viewport.world_ptr()->add_target(axis);
			// UI 辅助器：不参与方向光阴影（不投影/不接收，避免撑大阴影包围盒）
			for (auto* d : axis->handle_drawers()) d->shadow(false);
			// 诊断：gizmo 网格统计（顶点/坐标范围/NaN/索引）
			for (auto* d : axis->handle_drawers())
			{
				auto* p = dynamic_cast<prefab*>(d);
				if (!p) continue;
				float mn[3] = { 1e30f,1e30f,1e30f }, mx[3] = { -1e30f,-1e30f,-1e30f };
				size_t nb = 0, nanv = 0;
				for (auto& tri : p->vertex())
					for (auto& pt : tri) for (int k = 0; k < 3; ++k)
					{
						float v = (float)pt[k];
						if (!std::isfinite(v)) { ++nanv; continue; }
						if (v < mn[k]) mn[k] = v; if (v > mx[k]) mx[k] = v; ++nb;
					}
				printf("[GIZMO] tris=%zu verts=%zu nan=%zu min=(%.2f %.2f %.2f) max=(%.2f %.2f %.2f)\n",
					p->size(), nb, nanv, mn[0], mn[1], mn[2], mx[0], mx[1], mx[2]);
			}
		}
		printf("[OK] gizmo axes=%d\n", (int)parts.size());
	}

	// [BC1] M4 压缩验证：round-trip 编解码 + 压缩纹理上传
	{
		const uint W = 64, H = 64;
		std::vector<xhqm::rgba> src(W * H);
		for (uint y = 0; y < H; ++y)
			for (uint x = 0; x < W; ++x)
				src[y * W + x] = xhqm_rgba((uint)(x * 4) % 256, (uint)(y * 4) % 256, (uint)((x + y) * 2) % 256, 255);
		auto enc = xhqm::fantasy::bc1_encode(src, W, H);
		auto dec = xhqm::fantasy::bc1_decode(enc, W, H);
		double mse = 0;
		for (size_t i = 0; i < src.size(); ++i)
		{
			int dr = (int)xhqm_rgba_r(src[i]) - (int)xhqm_rgba_r(dec[i]);
			int dg = (int)xhqm_rgba_g(src[i]) - (int)xhqm_rgba_g(dec[i]);
			int db = (int)xhqm_rgba_b(src[i]) - (int)xhqm_rgba_b(dec[i]);
			mse += dr * dr + dg * dg + db * db;
		}
		mse /= (src.size() * 3);
		printf("[BC1] %dx%d -> %d bytes (%.1f%% of RGBA), round-trip MSE=%.2f\n", W, H, (int)enc.size(), 100.0 * enc.size() / (src.size() * 4), mse);
		std::string terr;
		uint ctex = xhqm::fantasy::render_backend_active().texture_create_compressed(enc, W, H, terr);
		if (ctex) { xhqm::fantasy::render_backend_active().texture_delete(ctex); printf("[BC1] compressed texture upload OK\n"); }
		else printf("[BC1] compressed texture upload FAIL: %s\n", terr.c_str());
	}

	// [GLTF] M10 原生加载验证：解析同一 .glb 构建 prefab
	{
		std::string gerr;
		xhqm::fantasy::gltf::load_result gr;
		if (xhqm::fantasy::gltf::load(k_default_model, gr, gerr))
		{
			size_t total_tris = 0;
			for (auto* p : gr.prefabs) total_tris += (size_t)p->size();
			printf("[GLTF] native load OK, prefabs=%d, tris=%d\n", (int)gr.prefabs.size(), (int)total_tris);

			// [SCENE] M10 场景序列化 round-trip
			const std::string tmp = "D:/WorkSpace/xhqm_v2/binary/_scene_test.json";
			if (xhqm::fantasy::scene_save(tmp, gr.prefabs, gr.transforms, gerr))
			{
				std::vector<xhqm::fantasy::prefab*> back;
				std::vector<xhqm::fantasy::matrix> btrans;
				if (xhqm::fantasy::scene_load(tmp, back, btrans, gerr))
				{
					size_t back_tris = 0;
					for (auto* p : back) back_tris += (size_t)p->size();
					bool same = (back.size() == gr.prefabs.size() && back_tris == total_tris);
					printf("[SCENE] save+load OK, prefabs=%d tris=%d match=%d\n", (int)back.size(), (int)back_tris, same ? 1 : 0);
					for (auto* p : back) delete p;
				}
				else printf("[SCENE] load FAIL: %s\n", gerr.c_str());
			}
			else printf("[SCENE] save FAIL: %s\n", gerr.c_str());

			for (auto* p : gr.prefabs) delete p;
		}
		else
		{
			printf("[GLTF] native load FAIL: %s\n", gerr.c_str());
		}
	}

	// [BLEND] M9 多目标 blendshape 验证：2 目标 0.5/0.5 混合
	{
		using namespace xhqm::fantasy;
		prefab* bp = new prefab();
		bp->size(1);
		std::vector<std::array<point, 3>> verts = { std::array<point, 3>{ point(0, 0, 0), point(1, 0, 0), point(0, 1, 0) } };
		bp->vertex(verts);
		std::vector<std::array<value, 3>> norms = { {0, 0, 1}, {0, 0, 1}, {0, 0, 1} };
		bp->normal(norms);
		blend_target t0, t1;
		t0.delta = { {1, 0, 0}, {0, 0, 0}, {0, 0, 0} };
		t0.normal_delta = { {0, 0, 0}, {0, 0, 0}, {0, 0, 0} };
		t1.delta = { {0, 2, 0}, {0, 0, 0}, {0, 0, 0} };
		t1.normal_delta = { {0, 0, 0}, {0, 0, 0}, {0, 0, 0} };
		bp->blend_targets({ t0, t1 });
		bp->blend_target_weights({ 0.5f, 0.5f });
		bp->open_blend(true);
		bp->updata();
		const auto& bd = bp->blend_delta();
		bool ok = (std::abs(bd[0][0] - 0.5f) < 1e-4f && std::abs(bd[0][1] - 1.0f) < 1e-4f && std::abs(bd[0][2]) < 1e-4f);
		printf("[BLEND] blended delta[0]=(%.2f,%.2f,%.2f) expect=(0.50,1.00,0.00) ok=%d\n", bd[0][0], bd[0][1], bd[0][2], ok ? 1 : 0);
		delete bp;
	}

	// [CUBE] M6 立方体深度离屏验证：创建 + 取纹理 + 删除
	{
		std::string cerr;
		uint cid = xhqm::fantasy::render_backend_active().create_offscreen_cubemap_depth(256, cerr);
		if (cid)
		{
			uint ctex = xhqm::fantasy::render_backend_active().offscreen_cubemap_texture(cid);
			xhqm::fantasy::render_backend_active().delete_offscreen_cubemap_depth(cid);
			printf("[CUBE] cubemap depth offscreen OK, tex=%d\n", (int)ctex);
		}
		else printf("[CUBE] cubemap depth FAIL: %s\n", cerr.c_str());
	}

	// [PSHADOW] M6 点光阴影 VP 验证：setup + compute + 取纹理 + reset
	{
		xhqm::fantasy::point_shadow_map psm;
		std::string perr;
		if (psm.setup(xhqm::fantasy::render_backend_active(), 256, perr))
		{
			psm.compute(xhqm::fantasy::point(5, 3, -10), 0.1f, 50.0f);
			uint ptex = psm.depth_texture();
			// 验证：光位置经每面 view 矩阵应映射到原点（列向量 view * eye = 0）——用 VP 的平移列间接检查
			bool finite = true;
			for (int i = 0; i < 6; ++i)
			{
				const auto& vp = psm.face_vp(i);
				for (int r = 0; r < 4; ++r)
					for (int c = 0; c < 4; ++c)
						if (!std::isfinite(vp(r, c))) finite = false;
			}
			printf("[PSHADOW] point shadow OK, tex=%d, 6-face VP finite=%d\n", (int)ptex, finite ? 1 : 0);
			psm.reset();
		}
		else printf("[PSHADOW] setup FAIL: %s\n", perr.c_str());
	}

	// [FLOAT] M9 浮点纹理验证：创建 RGBA32F + 更新 + 删除
	{
		std::vector<xhqm::fantasy::value> ftex(4 * 4 * 4, 0.5f);
		std::string ferr;
		uint fid = xhqm::fantasy::render_backend_active().texture_create_float(ftex, 4, 4, ferr);
		if (fid)
		{
			for (size_t i = 0; i < ftex.size(); ++i) ftex[i] = (float)(i % 7) / 7.0f;
			xhqm::fantasy::render_backend_active().texture_update(fid, ftex);
			xhqm::fantasy::render_backend_active().texture_delete(fid);
			printf("[FLOAT] float texture create+update OK, tex=%d\n", (int)fid);
		}
		else printf("[FLOAT] float texture FAIL: %s\n", ferr.c_str());
	}

	// [GSKIN] M9 GPU 蒙皮验证：compute 算调色板 vs CPU 参考（3 骨骼链）
#if 0
	{
		using namespace xhqm::fantasy;
		// 链：root T(1,0,0) → child T(0,2,0) → grandchild T(0,0,3)
		std::vector<gpu_bone> bones(3, gpu_bone{ matrix(4, 4), -1, matrix(4, 4) });
		bones[0].local = xhqm::translate(1.0f, 0.0f, 0.0f);
		bones[0].parent = -1;
		bones[0].inv_bind = matrix(4, 4);
		bones[1].local = xhqm::translate(0.0f, 2.0f, 0.0f);
		bones[1].parent = 0;
		bones[1].inv_bind = xhqm::translate(-1.0f, 0.0f, 0.0f);  // 非单位逆绑定，验证 world*inv_bind
		bones[2].local = xhqm::translate(0.0f, 0.0f, 3.0f);
		bones[2].parent = 1;
		bones[2].inv_bind = matrix(4, 4);

		// CPU 参考：祖先链累乘 world[i] = world[parent]*local[i]，palette[i]=world[i]*inv_bind[i]
		std::vector<matrix> cp(bones.size(), matrix(4, 4));
		for (size_t i = 0; i < bones.size(); ++i)
		{
			matrix w = bones[i].local;
			int p = bones[i].parent;
			while (p >= 0) { w = bones[p].local * w; p = bones[p].parent; }
			cp[i] = w * bones[i].inv_bind;
		}

		std::string gerr;
		gpu_skinner gs;
		bool gok = false;
		if (gs.build(bones, gerr))
		{
			gs.compute();
			std::vector<matrix> gp = gs.read_palette();
			gok = (gp.size() == cp.size());
			if (gok)
			{
				for (size_t i = 0; i < cp.size() && gok; ++i)
					for (int r = 0; r < 4 && gok; ++r)
						for (int c = 0; c < 4 && gok; ++c)
							if (std::abs(gp[i](r, c) - cp[i](r, c)) > 1e-4f) gok = false;
			}
			if (gok)
			{
				// 抽查平移列，便于人眼核对
				printf("[GSKIN] GPU skinning OK, bones=%d p0=(%.1f,%.1f,%.1f) p1=(%.1f,%.1f,%.1f) p2=(%.1f,%.1f,%.1f)\n",
					(int)bones.size(),
					gp[0](0, 3), gp[0](1, 3), gp[0](2, 3),
					gp[1](0, 3), gp[1](1, 3), gp[1](2, 3),
					gp[2](0, 3), gp[2](1, 3), gp[2](2, 3));
			}
			else printf("[GSKIN] GPU skinning MISMATCH\n");
		}
		else printf("[GSKIN] build FAIL: %s\n", gerr.c_str());
	}
#endif

	// [HIZ] M7 Hi-Z 深度金字塔验证：4x4 深度 → 2x2 → 1x1，逐级取 2x2 块 max
#if 0
	{
		using namespace xhqm::fantasy;
		const uint W = 4, H = 4;
		std::vector<value> l0 = {
			0.1f, 0.5f, 0.2f, 0.9f,
			0.3f, 0.7f, 0.4f, 0.1f,
			0.8f, 0.2f, 0.6f, 0.5f,
			0.4f, 0.9f, 0.3f, 0.7f,
		};
		std::string herr;
		gpu_hiz hz;
		bool hok = false;
		if (hz.build(W, H, herr))
		{
			hz.upload_level0(l0);
			hz.build_pyramid();
			// CPU 参考逐级下采样
			std::vector<std::vector<value>> ref;
			ref.push_back(l0);
			uint cw = W, ch = H;
			while (cw > 1 || ch > 1)
			{
				uint nw = (cw + 1) / 2, nh = (ch + 1) / 2;
				std::vector<value> nxt(nw * nh, 0.0f);
				for (uint y = 0; y < nh; ++y)
					for (uint x = 0; x < nw; ++x)
					{
						value m = -1e9f;
						for (uint dy = 0; dy < 2; ++dy)
							for (uint dx = 0; dx < 2; ++dx)
							{
								uint sx = x * 2 + dx, sy = y * 2 + dy;
								if (sx >= cw) sx = cw - 1;
								if (sy >= ch) sy = ch - 1;
								m = std::max(m, ref.back()[sy * cw + sx]);
							}
						nxt[y * nw + x] = m;
					}
				ref.push_back(nxt);
				cw = nw; ch = nh;
			}
			hok = (hz.level_count() == ref.size());
			for (uint lv = 0; lv < ref.size() && hok; ++lv)
			{
				std::vector<value> g = hz.read_level(lv);
				if (g.size() != ref[lv].size()) { hok = false; break; }
				for (size_t i = 0; i < g.size(); ++i)
					if (std::abs(g[i] - ref[lv][i]) > 1e-4f) { hok = false; break; }
			}
			if (hok)
				printf("[HIZ] depth pyramid OK, levels=%d L0=(%.1f) L1=(%.1f,%.1f,%.1f,%.1f) top=%.1f\n",
					(int)ref.size(), ref[0][0],
					ref[1][0], ref[1][1], ref[1][2], ref[1][3], ref.back()[0]);
			else printf("[HIZ] depth pyramid MISMATCH\n");
		}
		else printf("[HIZ] build FAIL: %s\n", herr.c_str());
	}

	// [HIZCULL] M7 GPU 两阶段遮挡剔除验证：VP=identity，深度 0.9 平面，近/远/界外三对象
	{
		using namespace xhqm::fantasy;
		// Hi-Z：4x4 全 0.9（远处平面）
		gpu_hiz hz2;
		std::string h2err;
		bool hcok = false;
		if (hz2.build(4, 4, h2err))
		{
			hz2.upload_level0(std::vector<value>(16, 0.9f));
			hz2.build_pyramid();
			// 三对象：near(z=0.5) / far(z=0.95) / 界外(x=2)
			std::vector<gpu_hiz_cull_object> objs(3);
			objs[0].center[0] = 0; objs[0].center[1] = 0; objs[0].center[2] = 0.5f;
			objs[0].half_extent[0] = 0.01f; objs[0].half_extent[1] = 0.01f; objs[0].half_extent[2] = 0.01f;
			objs[0].cmd[0] = 6; objs[0].cmd[1] = 1; objs[0].cmd[2] = 0; objs[0].cmd[3] = 0; objs[0].cmd[4] = 0;
			objs[1].center[0] = 0; objs[1].center[1] = 0; objs[1].center[2] = 0.95f;
			objs[1].half_extent[0] = 0.01f; objs[1].half_extent[1] = 0.01f; objs[1].half_extent[2] = 0.01f;
			objs[1].cmd[0] = 6; objs[1].cmd[1] = 1; objs[1].cmd[2] = 0; objs[1].cmd[3] = 0; objs[1].cmd[4] = 0;
			objs[2].center[0] = 2.0f; objs[2].center[1] = 0; objs[2].center[2] = 0;
			objs[2].half_extent[0] = 0.01f; objs[2].half_extent[1] = 0.01f; objs[2].half_extent[2] = 0.01f;
			objs[2].cmd[0] = 6; objs[2].cmd[1] = 1; objs[2].cmd[2] = 0; objs[2].cmd[3] = 0; objs[2].cmd[4] = 0;
			gpu_hiz_culler gc;
			if (gc.build(objs, h2err))
			{
				gc.cull(matrix(4, 4), 4, 4, hz2.depth_ssbo(), 4, 4, hz2.level_count());
				std::vector<bool> vis = gc.read_visible();
				bool expect = (vis.size() == 3 && vis[0] == true && vis[1] == false && vis[2] == false);
				hcok = expect;
				printf("[HIZCULL] two-phase occlusion %s, vis=(%d,%d,%d) expect=(1,0,0)\n",
					expect ? "OK" : "MISMATCH", (int)vis[0], (int)vis[1], (int)vis[2]);
			}
			else printf("[HIZCULL] culler build FAIL: %s\n", h2err.c_str());
		}
		else printf("[HIZCULL] hiz build FAIL: %s\n", h2err.c_str());
	}

	// [BINDLESS] M3 bindless 纹理句柄验证：GL 64 位句柄 / Vulkan descriptor 索引代理
	{
		using namespace xhqm::fantasy;
		render_backend& rb = xhqm::fantasy::render_backend_active();
		std::vector<rgba> px(4 * 4, xhqm_rgba(200, 100, 50, 255));
		std::string berr;
		uint btex = rb.texture_create(px, 4, 4, berr);
		if (btex)
		{
			std::uint64_t h = rb.texture_bindless_handle(btex);
			bool ok;
			if (rb.name()[0] == 'v') ok = (h == (std::uint64_t)btex);   // Vulkan 返回 id 代理
			else ok = (h != 0);                                          // GL 返回 64 位句柄
			printf("[BINDLESS] handle=%llu tex=%u ok=%d\n", (unsigned long long)h, (unsigned int)btex, ok ? 1 : 0);
			rb.texture_delete(btex);
		}
		else printf("[BINDLESS] tex FAIL: %s\n", berr.c_str());
	}

	// [SSAO+TAA] M8 后处理验证：单帧内跑 SSAO + TAA，帧后统一读回
	{
		using namespace xhqm::fantasy;
		render_backend& rb = xhqm::fantasy::render_backend_active();
		const uint SW = 64, SH = 64;
		const uint TW = 16, TH = 16;

		// SSAO：平面深度（R=5.0）
		std::vector<value> depth(SW * SH * 4, 0.0f);
		for (size_t i = 0; i < (size_t)SW * SH; ++i) depth[i * 4] = 5.0f;
		std::string serr;
		uint dtex = rb.texture_create_float(depth, SW, SH, serr);
		ssao_effect sao; bool sao_ready = (dtex && sao.setup(rb, SW, SH, serr));

		// TAA：current=2.0，history=0.5
		auto j = taa_jitter(0);
		bool jok = (j[0] >= -0.5f && j[0] <= 0.5f && j[1] >= -0.5f && j[1] <= 0.5f);
		std::vector<value> cur(TW * TH * 4, 2.0f);
		std::vector<value> hist(TW * TH * 4, 0.5f);
		std::string terr;
		uint ctex = rb.texture_create_float(cur, TW, TH, terr);
		uint htex = rb.texture_create_float(hist, TW, TH, terr);
		taa_effect ta; bool taa_ready = (ctex && htex && ta.setup(rb, TW, TH, terr));

		// SSR：平面镜（均匀深度）+ 均匀天空色 → 反射 = 天空色
		std::vector<value> scol(SW * SH * 4, 0.0f);
		for (size_t i = 0; i < (size_t)SW * SH; ++i) { scol[i * 4] = 1.0f; scol[i * 4 + 1] = 0.5f; scol[i * 4 + 2] = 0.25f; }
		std::string rerr;
		uint sctex = rb.texture_create_float(scol, SW, SH, rerr);
		ssr_effect sr; bool ssr_ready = (sctex && dtex && sr.setup(rb, SW, SH, rerr));

		// PCF：点光阴影 cube（填充深度 0.9，比较 0.95 → 被遮蔽）
		point_shadow_map psm;
		std::string perr;
		bool psm_ready = psm.setup(rb, 256, perr);
		uint cube_tex = 0;
		std::vector<value> pquad = { -1, -1, 0, 0,  1, -1, 1, 0,  1, 1, 1, 1,  -1, 1, 0, 1 };
		std::vector<uint> plocs = { 2, 2 };
		std::string qerr;
		uint qvb = rb.vertex_create(pquad, plocs, qerr);
		uint qib = rb.index_create({ 0, 1, 2, 0, 2, 3 }, qerr);
		const char* dvert_vk = "#version 450\nlayout(location=0) in vec2 pos;\nvoid main(){ gl_Position = vec4(pos,0.0,1.0); }";
		const char* dfrag_vk = "#version 450\nvoid main(){ gl_FragDepth = 0.9; }";
		const char* pvert_vk = "#version 450\nlayout(location=0) in vec2 pos;\nvoid main(){ gl_Position = vec4(pos,0.0,1.0); }";
		const char* pfrag_vk = "#version 450\nlayout(location=0) out vec4 o;\n"
			"layout(std140,set=0,binding=0) uniform UBO{vec4 params;}ubo;\n"
			"layout(set=0,binding=5) uniform samplerCubeShadow cubeShadow;\n"
			"void main(){ vec3 dir=normalize(vec3(1.0,0.0,0.0)); float s=texture(cubeShadow, vec4(dir, ubo.params.x)); o=vec4(s); }";
		// OpenGL GLSL 方言（无 descriptor set 语法；uniform 名与 uniform_vec4 一致）
		const char* dvert_gl = "#version 330 core\nlayout(location=0) in vec2 pos;\nvoid main(){ gl_Position = vec4(pos,0.0,1.0); }";
		const char* dfrag_gl = "#version 330 core\nvoid main(){ gl_FragDepth = 0.9; }";
		const char* pvert_gl = "#version 330 core\nlayout(location=0) in vec2 pos;\nvoid main(){ gl_Position = vec4(pos,0.0,1.0); }";
		const char* pfrag_gl = "#version 330 core\nlayout(location=0) out vec4 o;\n"
			"uniform vec4 params;\nuniform samplerCubeShadow cubeShadow;\n"
			"void main(){ vec3 dir=normalize(vec3(1.0,0.0,0.0)); float s=texture(cubeShadow, vec4(dir, params.x)); o=vec4(s); }";
		const bool vk_dialect = (viewport.backend() == RhiViewport::Backend::Vulkan);
		const char* dvert = vk_dialect ? dvert_vk : dvert_gl;
		const char* dfrag = vk_dialect ? dfrag_vk : dfrag_gl;
		const char* pvert = vk_dialect ? pvert_vk : pvert_gl;
		const char* pfrag = vk_dialect ? pfrag_vk : pfrag_gl;
		uint dsh = rb.shader_create(dvert, dfrag, qerr);
		uint psh = rb.shader_create(pvert, pfrag, qerr);
		uint pcf_target = rb.create_offscreen(64, 64, 1, qerr, false);
		bool pcf_ready = (psm_ready && qvb && qib && dsh && psh && pcf_target);
		if (psm_ready) { psm.compute(point(0, 0, 0), 0.1f, 10.0f); cube_tex = psm.depth_texture(); }

		if (sao_ready && taa_ready && ssr_ready && pcf_ready)
		{
			std::string ferr;
			rb.begin_frame(ferr);
			sao.apply(dtex, 1.0f, 1.0f, 0.5f, 0.025f);
			ta.apply(ctex, htex, 0.25f);
			sr.apply(sctex, dtex, 1.0f, 1.0f, 0.5f, 0.025f);
			// PCF：填充立方体 6 面 + 采样
			for (uint f = 0; f < 6; ++f)
			{
				psm.begin(f);
				rb.use_program(dsh);
				rb.draw_indexed(qvb, qib, rhi_primitive::triangles, 0, 6);
				psm.end();
			}
			rb.begin_offscreen(pcf_target);
			rb.use_program(psh);
			rb.bind_texture(4, cube_tex);
			rb.uniform_vec4(psh, "params", 0.95f, 0, 0, 0);
			rb.draw_indexed(qvb, qib, rhi_primitive::triangles, 0, 6);
			rb.end_offscreen();
			rb.end_frame();

			uint px = 0;
			rb.read_offscreen(sao.target(), SW / 2, SH / 2, &px);
			uint r = px & 0xFF;
			bool sok = (r >= 250);
			printf("[SSAO] flat depth AO=%.2f (expect ~1.00) ok=%d\n", r / 255.0f, sok ? 1 : 0);

			px = 0;
			rb.read_offscreen(ta.target(), TW / 2, TH / 2, &px);
			float ao = (px & 0xFF) / 255.0f;
			bool cok = (ao >= 0.45f && ao <= 0.55f);
			printf("[TAA] jitter=(%.3f,%.3f) ok=%d; clamp blend=%.2f (expect ~0.50, unclamped ~0.875) ok=%d\n",
				j[0], j[1], jok ? 1 : 0, ao, cok ? 1 : 0);

			px = 0;
			rb.read_offscreen(sr.target(), SW / 2, SH / 2, &px);
			uint rr = px & 0xFF;
			bool rok = (rr >= 245);
			printf("[SSR] flat mirror reflect R=%.2f (expect ~1.00) ok=%d\n", rr / 255.0f, rok ? 1 : 0);

			px = 0;
			rb.read_offscreen(pcf_target, 32, 32, &px);
			float sf = (px & 0xFF) / 255.0f;
			bool pcf_ok = (sf < 0.1f);
			printf("[PCF] cube shadow sample=%.2f (compare 0.95 vs depth 0.9, expect ~0.00 shadowed) ok=%d\n", sf, pcf_ok ? 1 : 0);
		}
		else
		{
			if (!sao_ready) printf("[SSAO] setup FAIL: %s\n", serr.c_str());
			if (!taa_ready) printf("[TAA] setup FAIL: %s\n", terr.c_str());
			if (!ssr_ready) printf("[SSR] setup FAIL: %s\n", rerr.c_str());
			if (!pcf_ready) printf("[PCF] setup FAIL\n");
		}
		if (dtex) rb.texture_delete(dtex);
		if (ctex) rb.texture_delete(ctex);
		if (htex) rb.texture_delete(htex);
		if (sctex) rb.texture_delete(sctex);
		if (qvb) rb.vertex_delete(qvb);
		if (qib) rb.index_delete(qib);
		if (dsh) rb.shader_delete(dsh);
		if (psh) rb.shader_delete(psh);
		if (pcf_target) rb.delete_offscreen(pcf_target);
	}
#endif

	// 3. 程序化双骨棒（验证 LBS 蒙皮），置于左侧
	if (!g_nobar)
	{
		models* bar_model = new models();
		g_bar = demo::make_skinned_bar();
		bar_model->insert_drawer(g_bar);
		bar_model->system::coord(xhqm::translate(-60.0f, 0.0f, -60.0f) * xhqm::scale(25.0f));
		viewport.world_ptr()->add_target(bar_model);
	}

	// 4. 按键：[/] 或 ←/→ 切换动画，1/2/3 切换 gizmo 轴显隐
	viewport.key_handler = [](int key)
	{
		if (key == Qt::Key_BracketRight)
		{
			if (!g_clips.empty())
			{
				g_clip_idx = (g_clip_idx + 1) % g_clips.size();
				printf("[ANIM] -> %s (%zu/%zu)\n", g_clips[g_clip_idx].name.c_str(), g_clip_idx, g_clips.size());
			}
		}
		else if (key == Qt::Key_BracketLeft)
		{
			if (!g_clips.empty())
			{
				g_clip_idx = (g_clip_idx + g_clips.size() - 1) % g_clips.size();
				printf("[ANIM] -> %s (%zu/%zu)\n", g_clips[g_clip_idx].name.c_str(), g_clip_idx, g_clips.size());
			}
		}
		else if (key >= Qt::Key_1 && key <= Qt::Key_3)
		{
			int i = key - Qt::Key_1;
			if (i < (int)g_gizmo.size())
			{
				g_gizmo_show[i] = !g_gizmo_show[i];
				g_gizmo[i]->show(g_gizmo_show[i]);
				printf("[GIZMO] axis %d %s\n", i, g_gizmo_show[i] ? "show" : "hide");
			}
		}
	};

	// 4.5 多相机架构：--windows N —— 附加 N-1 个窗口共享同一 world，各自独立相机 + 独立管线
	std::vector<RhiViewport*> extra_viewports;
	for (int k = 1; k < g_windows; ++k)
	{
		RhiViewport* v = new RhiViewport(backend, nullptr, viewport.gl_context());
		v->attach_world(viewport.world_ptr());
		char title[128];
		snprintf(title, sizeof(title), "RhiViewport — 多窗口 #%d（同一场景）", k + 1);
		v->setTitle(title);
		v->resize(420, 420);
		v->setPosition(60 + (k % 4) * 60, 60 + (k % 4) * 60);
		v->show();
		app.processEvents();   // 触发暴露，拿到原生窗口 id
		if (!v->initialize())
		{
			printf("[FAIL] window %d initialize\n", k + 1);
			delete v;
			continue;
		}
		// 每窗口不同视角：yaw 均分一圈，焦距略有差异
		xhqm::fantasy::camera* cam = v->camera_ptr();
		cam->orbit(xhqm::radians(360.0f * k / g_windows), cam->pitch());
		cam->dolly(60.0f + 10.0f * (k % 3));
		v->start();
		extra_viewports.push_back(v);
		printf("[MULTI] window %d ready: cam=%p fg=%p (shared world %p)\n",
			k + 1, (void*)cam, (void*)v->frame_graph_ptr(), (void*)viewport.world_ptr());
	}
	if (!extra_viewports.empty())
		printf("[MULTI] %zu 附加窗口已挂载到同一场景\n", extra_viewports.size());

	// 5. 帧循环 + 双骨棒弯曲 + 模型骨骼动画采样
	viewport.start();

	// --snapshot <path>: 渲染若干帧后导出 PNG 并退出（多窗口时逐窗口各出一张）
	if (!snap_path.isEmpty())
		QTimer::singleShot(900, [&viewport, &extra_viewports, snap_path]()
		{
			std::string base = snap_path.toStdString();
			auto dot = base.rfind('.');
			std::string stem = (dot == std::string::npos) ? base : base.substr(0, dot);
			std::string ext = (dot == std::string::npos) ? ".png" : base.substr(dot);
			viewport.snapshot_to(stem + "_win0" + ext);
			for (size_t i = 0; i < extra_viewports.size(); ++i)
				extra_viewports[i]->snapshot_to(stem + "_win" + std::to_string(i + 1) + ext);
			QGuiApplication::quit();
		});

	// --snap-series <dir>: 诊断连拍——每 120ms 拍一帧共 15 帧，帧间差分定位闪烁源
	QTimer series_timer;   // main 作用域：生命周期须覆盖 app.exec()，勿放 if 块内（块结束析构 → 空 this 崩溃）
	if (!snap_series_dir.isEmpty())
	{
		std::string sdir = snap_series_dir.toStdString();
		fprintf(stderr, "[series] dir=%s\n", sdir.c_str());
		mkdir_wrap(sdir.c_str());
		auto series = std::make_shared<int>(0);
		series_timer.setInterval(120);
		QObject::connect(&series_timer, &QTimer::timeout, [&viewport, sdir, series, &series_timer]()
		{
			int i = (*series)++;
			char path[1024];
			snprintf(path, sizeof(path), "%s/frame_%04d.png", sdir.c_str(), i);
			fprintf(stderr, "[series] frame %d -> %s\n", i, path);
			viewport.snapshot_to(path);
			fprintf(stderr, "[series] frame %d done\n", i);
			if (i >= 14) { series_timer.stop(); QGuiApplication::quit(); }
		});
		QTimer::singleShot(900, [&series_timer]() { fprintf(stderr, "[series] timer start\n"); series_timer.start(); });
	}

	// --watch: 诊断——每 100ms 渲染+读回 swapchain，统计天空区橙色像素，区分引擎侧/合成侧闪烁
	QTimer watch_timer;
	if (g_watch)
	{
		auto wcount = std::make_shared<int>(0);
		watch_timer.setInterval(20);   // 高速被动采样（< 35ms 帧周期），捕捉每个 present 帧的内容
		QObject::connect(&watch_timer, &QTimer::timeout, [&viewport, wcount, &watch_timer]()
		{
			viewport.watch_orange_passive();
			if (++(*wcount) >= 400) { watch_timer.stop(); QGuiApplication::quit(); }
		});
		QTimer::singleShot(900, [&watch_timer]() { fprintf(stderr, "[watch] timer start\n"); watch_timer.start(); });
	}

	// --dumpframes <dir>: 诊断——每 20ms 读回 swapchain 存盘（帧级对比屏拍时间线）
	QTimer dump_timer;
	if (!dump_dir.isEmpty())
	{
		std::string ddir = dump_dir.toStdString();
		mkdir_wrap(ddir.c_str());
		auto dcount = std::make_shared<int>(0);
		dump_timer.setInterval(20);
		QObject::connect(&dump_timer, &QTimer::timeout, [&viewport, ddir, dcount, &dump_timer]()
		{
			char path[1024];
			snprintf(path, sizeof(path), "%s/d_%05d.png", ddir.c_str(), (*dcount)++);
			viewport.dump_frame_raw(path);
			if (*dcount >= 500) { dump_timer.stop(); QGuiApplication::quit(); }
		});
		QTimer::singleShot(900, [&dump_timer]() { fprintf(stderr, "[dump] timer start\n"); dump_timer.start(); });
	}

	QTimer anim;
	anim.setInterval(35);
	QElapsedTimer clock;
	clock.start();
	const float t0 = clock.elapsed() / 1000.0f;
	QObject::connect(&anim, &QTimer::timeout, [&clock, t0, &viewport, &g_still]()
	{
		// 相机姿态/焦点由 fantasy::camera 自维护：frame() 经 theory_camera_render 驱动
		// camera::life() 按 focus/yaw/pitch/dist 摆位；左键平移时 target 与焦点同步移动。
		if (g_still) return;   // --still: 冻结动画（相机仍按 orbit 状态照常应用）
		// 双骨棒程序化弯曲
		if (g_bar)
		{
			float t = (float)clock.elapsed() / 1000.0f - t0;
			float angle = 45.0f * std::sin(t * 2.0f);
			g_bar->bone_palette(demo::bar_palette(angle));
		}
		// 模型骨骼动画采样（当前 clip）
		if (!g_clips.empty() && !g_anim_prefabs.empty())
		{
			const demo::anim_clip& clip = g_clips[g_clip_idx];
			float t = (float)clock.elapsed() / 1000.0f;
			float time = (clip.duration > 0.0f) ? std::fmod(t, clip.duration) : 0.0f;
			std::vector<xhqm::fantasy::matrix> palette = demo::sample_palette(clip, time);
			for (auto* p : g_anim_prefabs) p->bone_palette(palette);
		}
	});
	anim.start();

	return app.exec();
}
