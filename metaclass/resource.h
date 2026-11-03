#ifndef XHQM_RESOURCE
#define XHQM_RESOURCE
#include "../head.h"
#include <memory>

namespace xhqm 
{
    // 前置声明，用于友元声明
    template<typename T, typename ID>
    class resource;

    template<typename... Args>
    resource(Args&&... args) -> resource<typename std::tuple_element<0, std::tuple<Args...>>::type,
        std::conditional_t<sizeof...(Args) >= 2,
        typename std::tuple_element<1, std::tuple<Args...>>::type,
        int>>;


    template<typename _type, typename _id>
    class resource
    {
    public:
        std::shared_ptr<_type> share_ptr;
    public:
        resource() : share_ptr(std::make_shared<_type>()) {}
        resource(resource&& other) noexcept : share_ptr(other.share_ptr) {}
        resource(const resource& other) noexcept : share_ptr(other.share_ptr) {}

        template<typename... _argv_type>
        resource(_argv_type&& ... argv_data_)
        {
            // 检查是否试图用另一个resource对象构造
            static_assert(
                !(sizeof...(_argv_type) == 1 &&
                    (std::is_same_v<std::decay_t<_argv_type>, resource> || ...)),
                "Cannot construct resource from another resource object. Use clone() instead."
                );

            share_ptr = std::make_shared<_type>(std::forward<_argv_type>(argv_data_)...);
        }


        template<typename... _argv_type>
        void reset(_argv_type&& ... argv_data_)
        {
            share_ptr->reset(std::forward<_argv_type>(argv_data_)...);

        }
        void operator =(const resource& copy) noexcept
        {
            share_ptr = copy.share_ptr;
        }
        void operator =(resource&& move) noexcept
        {
            share_ptr = move.share_ptr;
        }
        operator _id() const
        {
            return _id(*share_ptr);
        }
    };
}
#endif
