#include "utility.h"
#include "system/filesys.h"
#include "system/libmange.h"
namespace xhqm
{
	namespace software
	{
		utility::utility()
		{
			object::type("utility");
		}

		utility::~utility()
		{
		}

		void utility::save(void)
		{
		}

		void utility::read(void)
		{
		}

		utility_root::utility_root()
		{
			object::type("utility_root");
		}

		utility_root::~utility_root()
		{
		}

		void utility_root::save(void)
		{
			std::string path = xhqm::filesys::current_path() + "/utility/utility.list";
			std::ofstream file(path);
			if (file.is_open())
			{
				auto childs = this->childs();
				for (auto& child_ptr : childs)
				{
					file << child_ptr->uuid() << std::endl;
				}
				file << std::endl;
				file.close();
			}
		}

		void utility_root::read(void)
		{
			std::string path = xhqm::filesys::current_path() + "/utility/utility.list";
			std::ifstream file(path);
			if (file.is_open())
			{
				std::string uuid = ""; file >> uuid;
				if (!uuid.empty())
				{
					path = xhqm::filesys::current_path() + "/utility/" + uuid;
					xhqm::libid id = xhqm::libmange::loadlib(path);
					libfunc make_utility = xhqm::libmange::function(id, "create_object");
					if (make_utility)
					{
						int a = 0;
					}
				}
				file.close();
			}
		}
	}
}