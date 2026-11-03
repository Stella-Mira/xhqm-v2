#ifndef XHQM_TOOL_INSTANCE
#define XHQM_TOOL_INSTANCE
#include "../../head.h"
#include <typeindex>

namespace xhqm
{
    void xhqm_io instance_manger(const std::type_index& type_index, void* (*static_type)(), void*& class_instance);

    template<typename class_type> class instance
	{
    private:
        static void* static_type() 
        {
            static class_type classinstance;
            return reinterpret_cast<void*>(&reinterpret_cast<char&>(classinstance));
        }
    public:
        static class_type& get()
        {
            static void* classinstance = nullptr;
            if (classinstance == nullptr)
                xhqm::instance_manger(typeid(class_type), &static_type, classinstance);
            return *reinterpret_cast<class_type*>(classinstance);
        }
	};

    template<typename class_type>
    inline class_type& getInstance()
    {
        return xhqm::instance<class_type>::get();
    }
};
#endif