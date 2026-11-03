#ifndef XHQM_FUNCTIONAL_CALL
#define XHQM_FUNCTIONAL_CALL

#include "function_type.h"
#include <queue>

namespace xhqm
{
	//分离式std::function

	template <class func_type>
	class function_task
	{
	public:
		using function_type = typename xhqm::function<func_type>::function_std;
		using return_type = typename xhqm::function<func_type>::return_type;
		using argvs_type = typename xhqm::function<func_type>::argvs_type;
	protected:
		function_type m_function;
		argvs_type m_cache;
	public:
		inline operator bool() { return m_function; };
		function_task(const function_task& fun) : m_function(fun.m_function), m_cache(fun.m_cache) {};
		function_task(function_task&& fun) noexcept : m_function(std::move(fun.m_function)), m_cache(std::move(fun.m_cache)) {};

		template <class... _argvs_type>
		function_task(func_type&& func, _argvs_type&&... cache)
			: m_function(std::forward<func_type>(func)), m_cache(std::forward<_argvs_type>(cache)...)
		{

		};

		template <class... _argvs_type>
		inline return_type call(_argvs_type&&... _argvs) const
		{
			return m_function(std::forward<_argvs_type>(_argvs)...);
		}

		inline void call() const
		{
			return std::apply(m_function, m_cache);
		}

		template <class... _argvs_type>
		inline return_type operator()(_argvs_type&&... _argvs) const
		{
			return m_function(std::forward<_argvs_type>(_argvs)...);
		}

		inline return_type operator()() const
		{
			return std::apply(m_function, m_cache);
		}
	};

	//推导指引
	template <class type, class...argvs> function_task(type, argvs...) -> function_task<typename xhqm::function_type_s<type>::function_type>;


	template <class func_type>
	class function_call
	{
	public:
		using function_type = typename xhqm::function<func_type>::function_std;
		using return_type = typename xhqm::function<func_type>::return_type;
		using argvs_type = typename xhqm::function<func_type>::argvs_type;
	protected:
		function_type m_function;
		std::queue<argvs_type> m_cache;
	public:
		inline operator bool() { return m_function; };
		function_call(const function_call& fun) : m_function(fun.m_function), m_cache(fun.m_cache) {};
		function_call(function_call&& fun) noexcept : m_function(std::move(fun.m_function)), m_cache(std::move(fun.m_cache)) {};
		function_call(func_type&& fun) : m_function(std::forward<func_type>(fun)) {};
		function_call() : m_function(nullptr) {};


		inline bool next()
		{
			if (m_cache.size()) m_cache.pop();
			return m_cache.size();
		}

		inline void operator=(func_type&& function) noexcept
		{
			m_function = std::forward<func_type>(function);
		}

		template <class... _argvs_type>
		inline void argvs(_argvs_type&&... _argvs) noexcept
		{
			m_cache.emplace(argvs_type(std::forward<_argvs_type>(_argvs)...));
		}

		template <class... _argvs_type>
		inline return_type call(_argvs_type&&... _argvs) const
		{
			return m_function(std::forward<_argvs_type>(_argvs)...);
		}

		inline void call() const
		{
			return std::apply(m_function, m_cache.front());
		}

		template <class... _argvs_type>
		inline return_type operator()(_argvs_type&&... _argvs) const
		{
			return m_function(std::forward<_argvs_type>(_argvs)...);
		}

		inline return_type operator()() const
		{
			return std::apply(m_function, m_cache.front());
		}
	};

	//推导指引
	template <class type, class...argvs> function_call(type, argvs...) -> function_call<typename xhqm::function_type_s<type>::function_type>;

}
#endif
