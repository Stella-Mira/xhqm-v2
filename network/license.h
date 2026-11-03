#ifndef XHQM_TOOL_LICENSE
#define XHQM_TOOL_LICENSE

#include "../head.h"
#include <functional>

namespace xhqm
{
	namespace license
	{
		struct xhqm_io server
		{
			xhqm::text license;		// 魔数
			xhqm::text version;		// 授权版本
			xhqm::text program;		// 授权软件
			xhqm::text publish;		// 授权人员
			xhqm::text company;		// 授权公司
			xhqm::text listhas;		// 许可范围
			xhqm::text timehas;		// 许可范围
		};

		struct xhqm_io client
		{
			xhqm::text idcard;		// 被授权标识
			xhqm::text author;		// 被授权客户
			xhqm::text device;		// 被授权设备
			xhqm::text remark;		// 被授权备注
		};

		xhqm_io xhqm::text all_version(void);
		xhqm_io xhqm::text all_program(void);
		xhqm_io xhqm::text all_listhas(void);
		xhqm_io xhqm::text all_timehas(void);

		xhqm_io std::string system_serial();
		xhqm_io std::string board_serial();
		xhqm_io std::string disk_serial();
		xhqm_io std::string cpu_serial();

		xhqm_io std::string get_idcard(void);	//生成唯一标识符
		xhqm_io std::string get_device(void);	//获取主板序列码

		license::server xhqm_io base64_to_server(const xhqm::text& base64);
		xhqm::text xhqm_io server_to_base64(const license::server& server);
		license::client xhqm_io base64_to_client(const xhqm::text& base64);
		xhqm::text xhqm_io client_to_base64(const license::client& client);
		bool xhqm_io read_license(const xhqm::text& base64, license::client& client, license::server& server);
		bool xhqm_io save_license(const license::client& client, const license::server& server, xhqm::text& base64);

		struct xhqm_io good
		{
			license::client client_data;
			license::server server_data;
			std::function<bool(const std::string&)> good_version;
			std::function<bool(const std::string&)> good_program;
			std::function<bool(const std::string&)> good_listhas;
			std::function<bool(const std::string&)> good_timehas;

			bool operator()(const std::string& license_path);
			bool operator>>(const std::string& license_path);
		};
	}
}

#endif
