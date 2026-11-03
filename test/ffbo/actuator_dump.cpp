// 诊断：输出模型真实的 actuator / joint 列表（排除 Python ctypes 结构体偏移误读）
// 用法：actuator_dump <fruitfly.xml>
#include <mujoco/mujoco.h>

#include <cstdio>
#include <cstring>

int main(int argc, char** argv)
{
	if (argc < 2) { std::printf("usage: actuator_dump <xml>\n"); return 1; }
	char err[2000] = "";
	mjModel* m = mj_loadXML(argv[1], nullptr, err, 2000);
	if (!m) { std::printf("load failed: %s\n", err); return 1; }

	std::printf("nq=%d nv=%d nu=%d na=%d nbody=%d njnt=%d\n",
		(int)m->nq, (int)m->nv, (int)m->nu, (int)m->na, (int)m->nbody, (int)m->njnt);

	std::printf("\n=== actuators (%d) ===\n", (int)m->nu);
	for (int j = 0; j < m->nu; ++j)
	{
		const char* nm = mj_id2name(m, mjOBJ_ACTUATOR, j);
		std::printf("%3d  %s\n", j, nm ? nm : "?");
	}

	std::printf("\n=== joints (%d) ===\n", (int)m->njnt);
	for (int j = 0; j < m->njnt; ++j)
	{
		const char* nm = mj_id2name(m, mjOBJ_JOINT, j);
		std::printf("%3d  %-24s type=%d dofadr=%d\n", j, nm ? nm : "?",
			(int)m->jnt_type[j], (int)m->jnt_dofadr[j]);
	}
	mj_deleteModel(m);
	return 0;
}
