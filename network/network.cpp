#include "network.h"

#include <csignal>

namespace xhqm
{
	namespace network
	{
		int init = 0;
		bool good = false;
		bool good_network()
		{
			return good;
		}

#ifdef XHQM_WINDOWS
		WSADATA wsaData;
		WORD sockVersion = MAKEWORD(2, 2);
		void init_network()
		{
			if (!good && init == 0)
			{
				good = (WSAStartup(sockVersion, &wsaData) == 0);
			}
			++init;
		}
		void exit_network()
		{
			--init;
			if (good && init == 0)
			{
				good = !(WSACleanup() == 0);
			}
		}
#else
		void init_network()
		{
			if (!good && init == 0)
			{
				// POSIX：向已关闭连接 send 默认触发 SIGPIPE 终止进程；
				// 网络层以返回值处理对端断开，必须忽略该信号（nebula 判死场景实测触发）
				std::signal(SIGPIPE, SIG_IGN);
				good = true;
			}
			++init;
		}
		void exit_network()
		{
			--init;
			if (good && init == 0)
			{
				good = false;
			}
		}
#endif


	}
}