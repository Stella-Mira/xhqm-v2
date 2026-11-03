#pragma once
#ifndef XHQM_CIPHER_BASE64_H
#define XHQM_CIPHER_BASE64_H

#include "../head.h"

namespace xhqm
{
	namespace cipher
	{
		struct xhqm_io base64
		{
			static xhqm::text encode(const std::vector<xhqm::byte>& data);

			static std::vector<xhqm::byte> decode(const xhqm::text& base);

			static xhqm::text encode_text(const xhqm::text& text)
			{
				std::vector<xhqm::byte> data(text.begin(), text.end());
				return cipher::base64::encode(data);
			}

			static xhqm::text decode_text(const xhqm::text& base)
			{
				std::vector<xhqm::byte> data = cipher::base64::decode(base);
				return xhqm::text(data.begin(), data.end());
			}
		};
	}
}

#endif // XHQM_CIPHER_BASE64_H