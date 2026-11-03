#include "md2.h"
#include <sstream>
#include <iomanip>

namespace xhqm
{
    namespace cipher
    {
        std::vector<xhqm::byte> md2::preprocess(const std::vector<xhqm::byte>& binary)
        {
            std::vector<xhqm::byte> result = binary;

            // PKCS#7填充
            size_t pad = 16 - (result.size() % 16);
            if (pad == 0) pad = 16;
            result.insert(result.end(), pad, static_cast<xhqm::byte>(pad));

            // 计算校验和
            std::vector<xhqm::byte> checksum(16, 0);
            xhqm::byte L = 0;

            for (size_t i = 0; i < result.size(); i += 16) {
                for (int j = 0; j < 16; ++j) {
                    xhqm::byte c = result[i + j];
                    //L = checksum[j] ^= S[c ^ L];
                    checksum[j] = checksum[j] ^ S[c ^ L];
                    L = checksum[j]; // 更新L为新的校验和值
                }
            }

            // 附加校验和
            result.insert(result.end(), checksum.begin(), checksum.end());
            return result;
        }

        std::vector<xhqm::byte> md2::preprocess(const std::string& message)
        {
            return preprocess(std::vector<xhqm::byte>(message.begin(), message.end()));
        }

        void md2::transform(std::vector<xhqm::byte>& state, const xhqm::byte* block)
        {
            for (int i = 0; i < 16; ++i) 
            {
                state[16 + i] = block[i];
                state[32 + i] = state[16 + i] ^ state[i];
            }

            xhqm::byte t = 0;
            for (int i = 0; i < 18; ++i) 
            {
                for (int j = 0; j < 48; ++j) 
                {
                    state[j] = t = state[j] ^ S[t];
                }
                t = static_cast<xhqm::byte>((t + i) & 0xFF);  // 添加类型转换
            }
        }

        xhqm::cipher::md2 md2::hash(const std::vector<xhqm::byte>& data)
        {
            std::vector<xhqm::byte> state(48, 0);
            for (size_t i = 0; i < data.size(); i += 16) 
            {
                transform(state, &data[i]);
            }
            std::copy(state.begin(), state.begin() + 16, digest.begin());
            return (*this);
        }
    }
}