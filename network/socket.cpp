#include "socket.h"
#include <sstream>
#include <climits>

namespace xhqm
{
	namespace network
	{
		sock::sock()
		{
		}

		sock::sock(hander sock)
			: sock_hander(sock)
		{

		}

		sock::sock(sock && move) noexcept
			: sock_hander(move.sock_hander)
		{
			move.sock_hander = error_socket;
		}

		sock::~sock()
		{
			if (good())
			{
				xhqm::network::close_socket(sock_hander);
			}
		}

		void sock::operator=(sock && move) noexcept
		{
			sock_hander = move.sock_hander;
			move.sock_hander = error_socket;
		}


		socket::socket(socket && move) noexcept
		{
			m_af = move.m_af;
			m_type = move.m_type;
			m_protocol = move.m_protocol;
			sock_addr = move.sock_addr;
			last_error = move.last_error;
			sock_hander = move.sock_hander;
			move.m_af = 0;
			move.m_type = 0;
			move.m_protocol = 0;
			move.sock_addr = "";
			move.last_error = 0;
			move.sock_hander = error_socket;
		}

		socket::socket(int af, int type, int protocol)
			: m_af(af), m_type(type), m_protocol(protocol)
		{
			sock_hander = xhqm::network::open_socket(af, type, protocol);
			if (!good()) { last_error = xhqm::network::error_code(); }
		}

		std::vector<std::string> split_path(const std::string& str, char tag)
		{
			std::string item;
			std::stringstream ss(str);
			std::vector<std::string> elems;
			while (std::getline(ss, item, tag))
				elems.push_back(item);
			return elems;
		}


		bool socket::ipv4path(const xhqm::string & path,
			xhqm::buffer<unsigned char>& host, unsigned short & post)
		{
			std::vector<std::string> list = split_path(path, ':');
			if (list.size() != 2) return false;
			post = std::stoi(list[1]);
			list = split_path(path, '.');
			if (list.size() != 4) return false;
			host.reset(4);
			host.value(0) = std::stoi(list[0]);
			host.value(1) = std::stoi(list[1]);
			host.value(2) = std::stoi(list[2]);
			host.value(3) = std::stoi(list[3]);
			return true;
		}

		bool socket::ipv6path(const xhqm::string & path,
			xhqm::buffer<unsigned short>& host, unsigned short & post)
		{
			return false;
		}

	}
}
