#include "instance.h"

#include <mutex>
#include <memory>
#include <unordered_map>

namespace xhqm
{
    struct _instance 
    {
        void* m_object = nullptr;
        std::shared_ptr<std::mutex> m_mutex = nullptr;
    };
    struct _instance_data 
    {
        std::mutex m_instance_mutex;
        std::unordered_map<std::type_index, _instance> m_instances_regist;
    };

    _instance_data* instance_data = nullptr;
    _instance* instance_regist(const std::type_index& typeIndex) {
        if(!instance_data)
            instance_data = new _instance_data();
        std::lock_guard<std::mutex> lock(instance_data->m_instance_mutex);

        std::unordered_map<std::type_index, _instance>::iterator itr = instance_data->m_instances_regist.find(typeIndex);
        if (itr != instance_data->m_instances_regist.end()) return &itr->second;

        std::pair<std::type_index, _instance> single_holder(typeIndex, _instance());
        itr = instance_data->m_instances_regist.insert(single_holder).first;

        _instance& oneinstance = itr->second;
        oneinstance.m_mutex = std::shared_ptr<std::mutex>(new std::mutex());
        oneinstance.m_object = nullptr;
        return &oneinstance;
    }

    void instance_manger(const std::type_index& type_index, void* (*static_type)(), void*& class_instance)
    {
        if (class_instance) return;
        _instance* single_ton_holder = instance_regist(type_index);
        std::lock_guard<std::mutex> lock(*single_ton_holder->m_mutex);
        single_ton_holder->m_object = single_ton_holder->m_object ? single_ton_holder->m_object : (*static_type)();
        class_instance = single_ton_holder->m_object;
    }
}