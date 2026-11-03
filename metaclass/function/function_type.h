#ifndef XHQM_FUNCTIONAL_TYPE
#define XHQM_FUNCTIONAL_TYPE

#include "function.h"
#include <stdexcept>

namespace xhqm 
{
    //函数接口
    template <class func_type>
    class function : public xhqm::ret_function_args<func_type>
    {
    public:
        using class_type = typename xhqm::ret_function_args<func_type>::class_type;
        using argvs_type = typename xhqm::ret_function_args<func_type>::argvs_type;
        using argvs_copy = typename xhqm::ret_function_args<func_type>::argvs_copy;
        using return_type = typename xhqm::ret_function_args<func_type>::return_type;
        using function_ptr = typename xhqm::ret_function_args<func_type>::function_ptr;
        using function_std = typename xhqm::ret_function_args<func_type>::function_std;
        using function_type = typename xhqm::ret_function_args<func_type>::function_type;
        operator bool() { return xhqm::ret_function_args<func_type>::_function_type_ptr; };
        //function(function&& function) = default;
        //function(const function&) noexcept = default;
        function(const function_ptr& function_type_ptr = nullptr) noexcept
        {
            this->_function_type_ptr = function_type_ptr;
        }
        template <class class_function> function(const class_function&) noexcept
        {
            this->_function_type_ptr = &class_function::operator();
        }
    };

    template <> class function<void>
    {
        void* m_argvs_ptr = nullptr;
        void* m_function_ptr = nullptr;

    public:
        operator bool() { return m_function_ptr; };
        function(function&&) = delete;
        function(const function&) = delete;
        function(std::nullptr_t null = nullptr) {};
        ~function()
        {
            if (m_argvs_ptr) std::runtime_error error("class function<void>::m_argvs_ptr");
            if (m_function_ptr) std::runtime_error error("class function<void>::m_function_ptr");
        }
        
        template <class func_type>
        void up_function(const xhqm::function<func_type>& _function) noexcept
        {
            if (m_function_ptr) delete static_cast<xhqm::function<func_type>*>(m_function_ptr);
            m_function_ptr = new xhqm::function<func_type>(_function);
        }
        template <class func_type>
        void up_function(std::nullptr_t null = nullptr) noexcept
        {
            if (m_function_ptr) delete static_cast<xhqm::function<func_type>*>(m_function_ptr);
            m_function_ptr = nullptr;
        }

        template <class func_type>
        void up_argvs(const typename xhqm::function<func_type>::argvs_type& _argvs) noexcept
        {
            using argvs_type = typename xhqm::ret_function_args<func_type>::argvs_type;
            if (m_argvs_ptr) delete static_cast<argvs_type*>(m_argvs_ptr);
            m_argvs_ptr = new argvs_type(_argvs);
        }

        template <class func_type>
        void up_argv(std::nullptr_t null = nullptr) noexcept
        {
            using argvs_type = typename xhqm::ret_function_args<func_type>::argvs_type;
            if (m_argvs_ptr) delete static_cast<argvs_type*>(m_argvs_ptr);
            m_argvs_ptr = nullptr;
        }


        template <class func_type>
        typename xhqm::function<func_type>::return_type call(const typename xhqm::function<func_type>::argvs_type& _argvs) noexcept
        {
            return std::apply(*static_cast<xhqm::function<func_type>*>(m_function_ptr), _argvs);
        }

        template <class func_type>
        typename xhqm::function<func_type>::return_type call() noexcept
        {
            using argvs_type = typename xhqm::ret_function_args<func_type>::argvs_type;
            return std::apply(*static_cast<xhqm::function<func_type>*>(m_function_ptr), *static_cast<argvs_type*>(m_argvs_ptr));
        }
    };

    //推导指引
    template <typename type = void, typename is_void = void> struct function_type_s
    {
        using function_type = type; function_type_s() = delete;
    };
    template <typename type> struct function_type_s<type, std::void_t<decltype(&type::operator())>>
    {
        using function_type = decltype(&type::operator()); function_type_s() = delete;
    };
    template <class type> function(type) -> function<typename xhqm::function_type_s<type>::function_type>;
    function(void) -> function<void>;
}
#endif
