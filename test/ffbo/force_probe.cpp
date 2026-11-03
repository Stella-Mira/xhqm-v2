// 最小验证：xfrc_applied 是否作用到质心 / freejoint 挂在哪个 body
// 用法：force_probe <fruitfly.xml>
#include <mujoco/mujoco.h>

#include <cstdio>
#include <cstring>

static void try_force(mjModel* m, const char* tag, int body, double fx, double fy, int steps)
{
	mjData* d = mj_makeData(m);
	d->qpos[2] = 0.3;
	mj_forward(m, d);
	for (int s = 0; s < 600; ++s) mj_step(m, d);   // 站稳

	double x0 = d->qpos[0], y0 = d->qpos[1];
	double vx_max = 0.0;
	for (int s = 0; s < steps; ++s)
	{
		if (body >= 0)
		{
			d->xfrc_applied[6 * body + 0] = fx;
			d->xfrc_applied[6 * body + 1] = fy;
			d->xfrc_applied[6 * body + 2] = 0.0;
		}
		mj_step(m, d);
		if (d->qvel[0] > vx_max) vx_max = d->qvel[0];
	}
	const double dt = m->opt.timestep * steps;
	std::printf("%-28s dx=%+.5f dy=%+.5f  v_x_max=%+.4f  (%.2fs)\n",
		tag, d->qpos[0] - x0, d->qpos[1] - y0, vx_max, dt);
	mj_deleteData(d);
}

int main(int argc, char** argv)
{
	if (argc < 2) { std::printf("usage: force_probe <xml>\n"); return 1; }
	char err[2000] = "";
	mjModel* m = mj_loadXML(argv[1], nullptr, err, 2000);
	if (!m) { std::printf("load failed: %s\n", err); return 1; }

	std::printf("nbody=%d njnt=%d nq=%d nv=%d  timestep=%.5f\n",
		(int)m->nbody, (int)m->njnt, (int)m->nq, (int)m->nv, m->opt.timestep);

	// 1) 找自由关节 / ball 关节，看挂在哪个 body
	std::printf("\n=== 关节一览（非 hinge/slide 的）===\n");
	for (int j = 0; j < m->njnt; ++j)
	{
		const int t = m->jnt_type[j];
		if (t == mjJNT_FREE || t == mjJNT_BALL)
		{
			const char* jn = mj_id2name(m, mjOBJ_JOINT, j);
			const int b = m->jnt_bodyid[j];
			const char* bn = mj_id2name(m, mjOBJ_BODY, b);
			std::printf("  joint %-10s type=%d(%s) body=%d(%s) qposadr=%d dofadr=%d\n",
				jn ? jn : "?", t, t == mjJNT_FREE ? "FREE" : "BALL",
				b, bn ? bn : "?", (int)m->jnt_qposadr[j], (int)m->jnt_dofadr[j]);
		}
	}

	// 2) 总质量
	double mtot = 0.0;
	for (int b = 1; b < m->nbody; ++b) mtot += m->body_mass[b];
	std::printf("\n总质量(除world) = %.6g kg\n", mtot);

	// 3) 找 freejoint 所在 body
	int free_body = -1;
	for (int j = 0; j < m->njnt; ++j)
		if (m->jnt_type[j] == mjJNT_FREE) { free_body = m->jnt_bodyid[j]; break; }
	const int thorax = mj_name2id(m, mjOBJ_BODY, "thorax");
	std::printf("freejoint body=%d(%s)  thorax=%d\n", free_body,
		free_body >= 0 ? mj_id2name(m, mjOBJ_BODY, free_body) : "?", thorax);

	// 4) 力扫描：找能克服地面静摩擦、真正推动果蝇的量级
	//    总质量仅 ~0.001 kg，0.5*mtot=4.9e-4 N 太小（2000 步=0.2s 也偏短，实测无位移）
	const int STEPS = 20000;   // 2 秒
	std::printf("\n=== 力扫描（向 +x，%d 步 = %.2f 秒）===\n", STEPS, m->opt.timestep * STEPS);
	try_force(m, "不施力（对照）", -1, 0, 0, STEPS);
	for (double f : { 0.0005, 0.005, 0.02, 0.05, 0.2, 1.0 })
	{
		char tag[64];
		std::snprintf(tag, sizeof(tag), "freejoint body  F=%.4g N", f);
		try_force(m, tag, free_body, f, 0, STEPS);
	}
	// 5) 反向确认方向可翻转
	std::printf("\n=== 反向验证 ===\n");
	try_force(m, "freejoint body  F=-0.2 N", free_body, -0.2, 0, STEPS);
	// 6) 非 freejoint body 作对照（应无位移，因为力加在非自由体上不影响质心）
	if (thorax >= 0 && thorax != free_body)
		try_force(m, "力施加到 thorax(非自由)", thorax, 0.2, 0, STEPS);

	mj_deleteModel(m);
	return 0;
}
