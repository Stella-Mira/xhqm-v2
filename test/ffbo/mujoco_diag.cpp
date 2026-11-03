// MuJoCo 果蝇模型诊断：body 世界位置 vs mesh 顶点范围（判断坐标系约定）
#include <mujoco/mujoco.h>
#include <cstdio>

int main(int argc, char** argv)
{
	if (argc < 2) { std::printf("usage: diag <fruitfly.xml>\n"); return 1; }
	char err[1000] = "";
	mjModel* m = mj_loadXML(argv[1], nullptr, err, 1000);
	if (!m) { std::printf("load fail: %s\n", err); return 1; }
	mjData* d = mj_makeData(m);
	mj_forward(m, d);

	std::printf("nbody=%d ngeom=%d nmesh=%d nq=%d\n",
		(int)m->nbody, (int)m->ngeom, (int)m->nmesh, (int)m->nq);

	std::printf("\n=== first 8 bodies (world xpos) ===\n");
	for (int b = 0; b < m->nbody && b < 8; ++b)
		std::printf("body[%2d] %-22s xpos=(%8.4f %8.4f %8.4f)\n", b,
			mj_id2name(m, mjOBJ_BODY, b),
			d->xpos[3 * b], d->xpos[3 * b + 1], d->xpos[3 * b + 2]);

	std::printf("\n=== first 6 mesh geoms ===\n");
	int shown = 0;
	for (int g = 0; g < m->ngeom && shown < 6; ++g)
	{
		if (m->geom_type[g] != mjGEOM_MESH) continue;
		const int mid = m->geom_dataid[g];
		const int adr = m->mesh_vertadr[mid], num = m->mesh_vertnum[mid];
		float mn[3] = { 1e9f, 1e9f, 1e9f }, mx[3] = { -1e9f, -1e9f, -1e9f };
		for (int v = 0; v < num; ++v)
			for (int k = 0; k < 3; ++k)
			{
				const float val = (float)m->mesh_vert[3 * (adr + v) + k];
				if (val < mn[k]) mn[k] = val;
				if (val > mx[k]) mx[k] = val;
			}
		const int bid = m->geom_bodyid[g];
		std::printf("geom[%2d] mesh=%-16s body=%-16s\n", g,
			mj_id2name(m, mjOBJ_MESH, mid), mj_id2name(m, mjOBJ_BODY, bid));
		std::printf("        gpos=(%.4f %.4f %.4f)  body_xpos=(%.4f %.4f %.4f)\n",
			m->geom_pos[3 * g], m->geom_pos[3 * g + 1], m->geom_pos[3 * g + 2],
			d->xpos[3 * bid], d->xpos[3 * bid + 1], d->xpos[3 * bid + 2]);
		std::printf("        mesh range = [%.3f..%.3f  %.3f..%.3f  %.3f..%.3f]\n",
			mn[0], mx[0], mn[1], mx[1], mn[2], mx[2]);
		++shown;
	}
	mj_deleteData(d);
	mj_deleteModel(m);
	return 0;
}
