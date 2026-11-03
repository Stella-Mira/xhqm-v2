
#include "xhqm/algebra"
static std::vector<xhqm::device> _devices;
static std::vector<xhqm::device> devices() 
{ 
    if (_devices.empty()) _devices.push_back({});
    std::size_t now_id = _devices.front().id;
    return _devices; 
};


/*
#include "xhqm/function"
#include "./geometry/point_measure.h"
#include "xhqm/tensot"
#include "xhqm/thread"
#include "include/system/timesys.h"
#include <complex>
#include "pattern/notifier.h"
#include "pattern/observer.h"
#include "system/codesys.h"
#include "./metaclass/struct/values.h"

std::mutex mmm;
xhqm::measure function(xhqm::measure& value, xhqm::count& count)
{
    std::lock_guard ll(mmm);
    std::this_thread::sleep_for(std::chrono::milliseconds{ 10 });
    count++;
    return value;
}

void thread_pool(xhqm::count& count)
{
    xhqm::measure value = 10.5;
    std::vector<xhqm::thread_task<decltype(&function)>> tasks(100, xhqm::thread_task(&function, value, count));
    xhqm::thread_pool pool(10);
    for (auto& task : tasks)
        while (!pool.run_task(&task)) {};
}

void thread_pool_add(xhqm::count& count)
{
    xhqm::measure value = 10.5;
    std::vector<xhqm::thread_task<decltype(&function)>> tasks(100, xhqm::thread_task(&function, value, count));
    xhqm::thread_pool pool(10);

    for (auto& task : tasks)
        pool.add_task(&task);
    pool.wait();
}
*/


int main()
{
    //xhqm::tree<int> value;


    xhqm::float_64 f64v = xhqm::number<xhqm::f64>::zero;
    xhqm::float_32 f32v = xhqm::number<xhqm::f32>::pi;
    xhqm::float_16 f16v = xhqm::number<xhqm::f16>::num_0;
    xhqm::float_16 f16u = 0b0000001111111111ui16;

    xhqm::min(f32v);

    xhqm::complex<double> i64v = xhqm::read<xhqm::complex<double>>("5.23565655565555565656565656565656565656565656565655656556565+3.5i");
    xhqm::string s64c = xhqm::save<xhqm::complex<double>>(i64v);
    //i64v = f16v;
    f64v = xhqm::abs(i64v);
    //std::complex<float> v = xhqm::min<float, std::complex>(std::complex<float>(0.f), std::complex<float>(1.f));
    //std::complex<float> v = xhqm::min<std::complex<float>>(std::complex<float>(0.f), std::complex<float>(1.f));
    std::complex<float> vv = xhqm::max(vv);
    std::cout << std::complex<float>(0.1f) << std::endl;
    std::cout << f32v << std::endl;
    std::cout << f16v << std::endl;
    std::cout << f16u << std::endl;
    //std::cout << f64v * f32v << std::endl;

    std::array<xhqm::size, 5> _cache = {};

    //xhqm::geometry::point_measure<0> i = { 1.0, 2.0 };
    //xhqm::geometry::point_measure<0> j = { 1.0, 2.0 };
    //xhqm::geometry::point_measure<0> k = i + j;
    //xhqm::cache<xhqm::size> i({2});

    //xhqm::topol j = arr;
    //bool a = i != arr;
    //bool b = i == j;

    //xhqm::count count = 11;
    //xhqm::measure value = 10.5;

    //std::string string = "星辉清梦";
    //std::vector string_src(string.begin(), string.end());
    //std::string base = xhqm::base64::encode_form(string_src);
    //std::vector<xhqm::writ> data = xhqm::base64::decode_to<xhqm::writ>(base);
    //std::string string_dsc(data.begin(), data.end());

    //std::cout << string_dsc << std::endl;
    //for (const auto& value : string_src) std::cout << value << "\t";
    //std::cout << std::endl << base << std::endl;
    //for (const auto& value : data) std::cout << value << "\t";


    //xhqm::function_queue func(&function);
    //func.argvs(value, count);
    //func.argvs(value, count);
    //func(value, count);
    //func();


    //xhqm::notifier<decltype(&thread_pool)> notifier;

    //notifier.regist(&thread_pool);
    //notifier.regist(&thread_pool_add);

    //notifier.notify(count);



    //xhqm::size len = 0;
    //xhqm::size star = 0;
    //star = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now()).time_since_epoch().count();
    //thread_pool_add(count);
    //len = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now()).time_since_epoch().count() - star;
    //std::cout << len << std::endl;

    //star = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now()).time_since_epoch().count();
    //thread_pool(count);
    //len = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now()).time_since_epoch().count() - star;
    //std::cout << len << std::endl;

    //xhqm::thread thread(&function);
    //thread.call(std::ref(value), std::ref(count));
    //value = thread.wait();
    //thread.call(std::ref(value), std::ref(count));
    //thread.wait();

    //xhqm::tensor<double, 3> tensor({ 3,5,2 }, 0.1);
    //std::array indexs = tensor.index(9);
    //xhqm::index index = tensor.index({ 0,3,0 });

    //auto shape = tensor.shape();
    //for (xhqm::index i = 0; i < shape[0]; ++i)
    //    for (xhqm::index j = 0; j < shape[1]; ++j)
    //        for (xhqm::index k = 0; k < shape[2]; ++k)
    //             tensor.value({ i, j, k }) = (i + 1.) * 100 + (j + 1.) * 10 + (k + 1.);

    //tensor.traverse([](const auto& values, const auto& index, double value, double div)
    //    {
    //        return (values[index] + value) / div;
    //    }, 3.5, 3.3);

    //for (xhqm::index i = 0; i < shape[0]; ++i)
    //{
    //    std::cout << std::endl << std::endl << std::endl;
    //    for (xhqm::index j = 0; j < shape[1]; ++j)
    //    {
    //        for (xhqm::index k = 0; k < shape[2]; ++k)
    //            std::cout << tensor(i, j, k) << ",";
    //        std::cout << std::endl;
    //    }
    //}

    
    //xhqm::parallel::array<double> values({ 10.0,20.0,30.0,40.0 });
    //for (int i = 0; i < 100000; ++i)
    //    values.push_back(i);

    //xhqm::parallel::dispatch<double> dispatch;
    //dispatch.compute(&function, values, false);
    //std::cout << values.back() << std::endl;
    //dispatch.wite();
    //std::cout << values.back() << std::endl;


    return 0;
}

