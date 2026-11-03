#ifndef XHQM_FUNCTIONAL
#define XHQM_FUNCTIONAL

#include "../template.h"
#include "class_type.h"
#include <functional>
#include <tuple>
namespace xhqm 
{
    //函数解析
    template <class type>
    struct is_function_s {
        inline constexpr static bool value = false;
        inline constexpr static bool is_const = false;
        inline constexpr static bool is_normal = false;
        inline constexpr static bool is_member = false;
    };

    template <class _return_type, class... _args_type>
    struct is_function_s<_return_type(_args_type...)>
    {
        inline constexpr static bool value = true;
        inline constexpr static bool is_const = false;
        inline constexpr static bool is_normal = true;
        inline constexpr static bool is_member = !is_normal;
    };

    template <class _return_type, class... _args_type>
    struct is_function_s<_return_type(*)(_args_type...)>
    {
        inline constexpr static bool value = true;
        inline constexpr static bool is_const = false;
        inline constexpr static bool is_normal = true;
        inline constexpr static bool is_member = !is_normal;
    };

    template <class _class_type, class _return_type, class... _args_type>
    struct is_function_s<_return_type(_class_type::*)(_args_type...)>
    {
        inline constexpr static bool value = true;
        inline constexpr static bool is_const = false;
        inline constexpr static bool is_normal = false;
        inline constexpr static bool is_member = !is_normal;
    };

    template <class _class_type, class _return_type, class... _args_type>
    struct is_function_s<_return_type(_class_type::*)(_args_type...) const>
    {
        inline constexpr static bool value = true;
        inline constexpr static bool is_const = true;
        inline constexpr static bool is_normal = false;
        inline constexpr static bool is_member = !is_normal;
    };

    template <class func_type>
    constexpr bool is_function = xhqm::is_function_s<func_type>::value;


    //函数封装
    typedef struct _ret_function_args_base { }*_ret_function_args_ptr;

    template <class _return_type, class... _args_type>
    struct _ret_function_args : public _ret_function_args_base {
        using argvs_type = std::tuple<_args_type...>;
        virtual _return_type call(_args_type&& ...) const = 0;
        _return_type operator()(_args_type ...values) const { return call(std::forward<_args_type>(values)...); };
    };

    //函数解析
    template <class type>
    struct ret_function_args 
    {
        static_assert(xhqm::always_false<type> && std::is_same_v<type, void>, "that is not function.");
    };
    template <class type> using retutn_function_argvs = xhqm::ret_function_args<type>;

    template <class _return_type, class... _args_type>
    struct ret_function_args<_return_type(_args_type...)>
        : public xhqm::_ret_function_args<_return_type, _args_type...>
    {
        using argvs_type = std::tuple<_args_type...>;
        using argvs_copy = std::tuple<xhqm::remove_const_ref<_args_type>...>;
        using class_type = void;
        using return_type = _return_type;
        using function_ptr = _return_type(*)(_args_type...);
        using function_std = std::function<_return_type(_args_type...)>;
        using function_type = function_ptr;

        constexpr static bool is_const = false;
        constexpr static bool is_normal = true;
        constexpr static bool is_member = !is_normal;

        virtual _return_type call(_args_type&& ...values) const override
        {
            return _function_type_ptr(std::forward<_args_type>(values)...);
        }
    protected:
        function_type _function_type_ptr = nullptr;
    };

    template <class _return_type, class... _args_type>
    struct ret_function_args<_return_type(*)(_args_type...)>
        : public xhqm::_ret_function_args<_return_type, _args_type...>
    {
        using argvs_type = std::tuple<_args_type...>;
        using argvs_copy = std::tuple<xhqm::remove_const_ref<_args_type>...>;
        using class_type = void;
        using return_type = _return_type;
        using function_ptr = _return_type(*)(_args_type...);
        using function_std = std::function<_return_type(_args_type...)>;
        using function_type = function_ptr;

        constexpr static bool is_const = false;
        constexpr static bool is_normal = true;
        constexpr static bool is_member = !is_normal;

        virtual _return_type call(_args_type&& ...values) const override
        {
            return _function_type_ptr(std::forward<_args_type>(values)...);
        }
    protected:
        function_type _function_type_ptr = nullptr;
    };

    template <class _class_type, class _return_type, class... _args_type>
    struct ret_function_args<_return_type(_class_type::*)(_args_type...)>
        : public xhqm::_ret_function_args<_return_type, _class_type*, _args_type...>
    {
        using argvs_type = std::tuple<_class_type*, _args_type...>;
        using argvs_copy = std::tuple<_class_type*, xhqm::remove_const_ref<_args_type>...>;
        using class_type = _class_type;
        using return_type = _return_type;
        using function_ptr = _return_type(_class_type::*)(_args_type...);
        using function_std = std::function<_return_type(_class_type*, _args_type...)>;
        using function_type = function_ptr;

        constexpr static bool is_const = false;
        constexpr static bool is_normal = false;
        constexpr static bool is_member = !is_normal;

        virtual _return_type call(_class_type*&& object_ptr, _args_type&& ...values) const override
        {
            return (object_ptr->*_function_type_ptr)(std::forward<_args_type>(values)...);
        }
    protected:
        function_type _function_type_ptr = nullptr;
    };

    template <class _class_type, class _return_type, class... _args_type>
    struct ret_function_args<_return_type(_class_type::*)(_args_type...) const>
        : public xhqm::_ret_function_args<_return_type, _class_type*, _args_type...>
    {
        using argvs_type = std::tuple<_class_type*, _args_type...>;
        using argvs_copy = std::tuple<_class_type*, xhqm::remove_const_ref<_args_type>...>;
        using class_type = _class_type;
        using return_type = _return_type;
        using function_ptr = _return_type(_class_type::*)(_args_type...) const;
        using function_std = std::function<_return_type(_class_type*, _args_type...)>;
        using function_type = function_ptr;

        constexpr static bool is_const = true;
        constexpr static bool is_normal = false;
        constexpr static bool is_member = !is_normal;

        virtual _return_type call(_class_type*&& object_ptr, _args_type&& ...values) const override
        {
            return (object_ptr->*_function_type_ptr)(std::forward<_args_type>(values)...);
        }
    protected:
        function_type _function_type_ptr = nullptr;
    };
}
#endif
