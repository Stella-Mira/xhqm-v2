#ifndef XHQM_SOFTWARE_PROJECT
#define XHQM_SOFTWARE_PROJECT

#include "packet.h"

namespace xhqm
{
	namespace software
	{
		class xhqm_io project
			: public software::packet
		{
		protected:

		public:
			explicit project();
			virtual ~project();
			project(project&&) = delete;
			project(const project&) = delete;

			bool push_packet(software::packet* packet_ptr)
			{
				return add_child_ptr(packet_ptr);
			}
			bool move_packet(software::packet* packet_ptr)
			{
				return sub_child_ptr(packet_ptr);
			}
		};

		class xhqm_io project_root 
			: public software::packet
		{
		public:
			explicit project_root();
			virtual ~project_root();
			project_root(project_root&&) = delete;
			project_root(const project_root&) = delete;

			virtual void save(void) override;
			virtual void read(void) override;

			std::vector<software::project*> projects()
			{
				std::vector<software::project*> _projects;

				auto childs = this->childs();
				for (auto& child_ptr : childs)
				{
					software::project* project_ptr = dynamic_cast<software::project*>(child_ptr);
					if (project_ptr) _projects.push_back(project_ptr);
				}

				return _projects;
			}
			software::project* project_ptr(const xhqm::index& index)
			{
				return dynamic_cast<software::project*>(this->child_ptr(index));
			}

			bool push_project(software::project* project_ptr)
			{
				return add_child_ptr(project_ptr);
			}
			bool move_project(software::project* project_ptr)
			{
				return sub_child_ptr(project_ptr);
			}
		};
	}
}

#endif