// 扫描初始高度：找出果蝇腿刚好触地、不穿地弹飞的高度
#include <mujoco/mujoco.h>
#include <cmath>
#include <cstdio>
#include <cstring>

static void drive(mjModel* m, mjData* d, double t)
{
	for (int i = 0; i < m->nu; ++i)
	{
		d->ctrl[i] = 0.0;
		const char* nm = mj_id2name(m, mjOBJ_ACTUATOR, i);
		if (!nm) continue;
		const bool seg = std::strstr(nm, "coxa") || std::strstr(nm, "femur")
			|| std::strstr(nm, "tibia") || std::strstr(nm, "tarsus");
		const bool nr = std::strstr(nm, "T1") || std::strstr(nm, "T2") || std::strstr(nm, "T3");
		if (!(seg && nr)) continue;
		const bool left = std::strstr(nm, "left") != nullptr;
		const bool mid = std::strstr(nm, "T2") != nullptr;
		const double phase = (left != mid) ? 0.0 : 3.14159265;
		double amp = 0.08;
		if (std::strstr(nm, "femur")) amp = 0.25;
		else if (std::strstr(nm, "coxa")) amp = 0.15;
		else if (std::strstr(nm, "tibia")) amp = 0.15;
		d->ctrl[i] = amp * std::sin(t * 6.0 + phase);
	}
}

int main(int argc, char** argv)
{
	if (argc < 2) { std::printf("usage: fly_drive_diag <fruitfly.xml>\n"); return 1; }

	const double heights[] = { 0.0003, 0.05, 0.1, 0.2, 0.3, 0.5 };
	std::printf("z0      final_pos(x,y,z)                  |v|      |w|     pen\n");

	for (double z0 : heights)
	{
		char err[1000] = "";
		mjModel* m = mj_loadXML(argv[1], nullptr, err, 1000);
		if (!m) { std::printf("load fail: %s\n", err); return 1; }
		mjData* d = mj_makeData(m);
		d->qpos[2] = z0;
		mj_forward(m, d);

		for (int s = 0; s < 1000; ++s)   // 1 秒
		{
			drive(m, d, d->time);
			mj_step(m, d);
		}
		const double* p = d->qpos;
		const double* v = d->qvel;
		const double vmag = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
		const double wmag = std::sqrt(v[3] * v[3] + v[4] * v[4] + v[5] * v[5]);
		std::printf("%6.4f  (%+8.4f %+8.4f %+8.4f)  %7.2f  %6.2f  %2d\n",
			z0, p[0], p[1], p[2], vmag, wmag, (int)d->ncon);
		mj_deleteData(d);
		mj_deleteModel(m);
	}
	return 0;
}
