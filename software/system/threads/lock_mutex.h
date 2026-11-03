#ifndef XHQM_LOCK_MUTEX
#define XHQM_LOCK_MUTEX

#include <shared_mutex>

namespace xhqm
{
	using lock_mutex = std::shared_mutex;
	class lock_get
	{
		std::shared_mutex& m_mutex;
	public:
		lock_get(std::shared_mutex& mutex)
			: m_mutex(mutex)
		{
			m_mutex.lock_shared();
		}

		~lock_get()
		{
			m_mutex.unlock_shared();
		}
	};
	class lock_set
	{
		std::shared_mutex& m_mutex;
	public:
		lock_set(std::shared_mutex& mutex)
			: m_mutex(mutex)
		{
			m_mutex.lock();
		}

		~lock_set()
		{
			m_mutex.unlock();
		}
	};
}

#endif