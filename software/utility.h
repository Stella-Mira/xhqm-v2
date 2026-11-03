#ifndef XHQM_SOFTWARE_UTILITY
#define XHQM_SOFTWARE_UTILITY

#include "packet.h"

namespace xhqm
{
	namespace software
	{
		class utility
			: public software::packet
		{
		public:
			explicit utility();
			virtual ~utility();
			utility(utility&&) = delete;
			utility(const utility&) = delete;
		public:
			virtual bool enter() = 0;
			virtual bool leave() = 0;
			virtual void save(void) override;
			virtual void read(void) override;
			virtual void rece(const std::string&) = 0;
		};
		
		class utility_root
			: public software::packet
		{
		public:
			explicit utility_root();
			virtual ~utility_root();
			utility_root(utility_root&&) = delete;
			utility_root(const utility_root&) = delete;
		public:
			virtual void save(void) override;
			virtual void read(void) override;

			bool push_utility(software::utility* utility_ptr)
			{
				return add_child_ptr(utility_ptr);
			}
			bool move_utility(software::utility* utility_ptr)
			{
				return sub_child_ptr(utility_ptr);
			}
		};
	}
}

#endif

