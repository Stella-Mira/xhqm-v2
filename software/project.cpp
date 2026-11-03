#include "project.h"
namespace xhqm
{
	namespace software
	{
		project::project()
		{
			object::type("project");
		}

		project::~project()
		{
		}

		project_root::project_root()
		{
			object::type("project_root");
		}

		project_root::~project_root()
		{
		}

		void project_root::save(void)
		{
		}

		void project_root::read(void)
		{
		}
	}
}