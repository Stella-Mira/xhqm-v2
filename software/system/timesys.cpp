#include "timesys.h"
#include <iomanip>
#include <chrono>
#include <ctime>
namespace xhqm
{
	xhqm::nowtime timesys::systemtime(xhqm::natural ms)
	{
		xhqm::nowtime xt;

		auto duration = std::chrono::milliseconds(ms);
		auto time_point = std::chrono::system_clock::time_point(duration);
		auto time = std::chrono::system_clock::to_time_t(time_point);

		// 获取本地时间信息
		std::tm local_time;
#ifdef XHQM_WINDOWS
		localtime_s(&local_time, &time);
#else
		localtime_r(&time, &local_time);
#endif

		// 提取毫秒部分
		auto since_epoch = time_point.time_since_epoch();
		auto seconds = std::chrono::duration_cast<std::chrono::seconds>(since_epoch);
		auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(since_epoch - seconds);

		// 填充结构体
		xt.year = static_cast<xhqm::integer>(local_time.tm_year + 1900);
		xt.month = static_cast<xhqm::integer>(local_time.tm_mon + 1);
		xt.mday = static_cast<xhqm::integer>(local_time.tm_mday);
		xt.wday = static_cast<xhqm::integer>(local_time.tm_wday);
		xt.hour = static_cast<xhqm::integer>(local_time.tm_hour);
		xt.minu = static_cast<xhqm::integer>(local_time.tm_min);
		xt.seco = static_cast<xhqm::integer>(local_time.tm_sec);
		xt.mill = static_cast<xhqm::integer>(milliseconds.count());

		return xt;
	}

	xhqm::nowtime timesys::systemtime()
	{
		return timesys::systemtime(timesys::mtime());
	}

	std::string timesys::localtime(xhqm::natural ms)
	{
		xhqm::nowtime time = timesys::systemtime(ms);
		return
			std::to_string(time.year) + "/" + std::to_string(time.month) + "/" + std::to_string(time.mday) + " " +
			std::to_string(time.hour) + ":" + std::to_string(time.minu) + ":" + std::to_string(time.seco) + ":" + std::to_string(time.mill);
	}

	std::string timesys::localtime()
	{
		xhqm::nowtime time = timesys::systemtime(timesys::mtime());
		return
			std::to_string(time.year) + "/" + std::to_string(time.month) + "/" + std::to_string(time.mday) + " " +
			std::to_string(time.hour) + ":" + std::to_string(time.minu) + ":" + std::to_string(time.seco) + ":" + std::to_string(time.mill);
	}

	std::string timer::old_time() const
	{
		return xhqm::timesys::localtime(this->m_starns / 1000000);
	}
	std::string timer::now_time() const
	{
		return xhqm::timesys::localtime(this->runtime() / 1000000);
	}
	std::string timer::run_time() const
	{
		xhqm::nowtime run = xhqm::timesys::systemtime((this->runtime() - this->m_starns) / 1000000);
		return std::to_string(run.hour) + ":" + std::to_string(run.minu) + ":" + std::to_string(run.seco) + "." + std::to_string(run.mill);
	}

	inline namespace literals
	{
		std::string operator""_systemtime(unsigned long long ms)
		{
			return xhqm::timesys::localtime(ms);
		}
	}

};
