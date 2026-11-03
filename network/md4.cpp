#include "md4.h"
#include <sstream>
#include <iomanip>

namespace xhqm
{
    namespace cipher
    {
        std::vector<xhqm::byte> md4::preprocess(const std::vector<xhqm::byte>& binary)
        {
            std::vector<xhqm::byte> result(binary.begin(), binary.end());
            uint64_t bitLen = result.size() * 8;
            result.push_back(0x80);
            while ((result.size() * 8) % 512 != 448) {
                result.push_back(0);
            }
            for (xhqm::size i = 0; i < 8; i++) {
                result.push_back(static_cast<xhqm::byte>(bitLen >> (i * 8)));
            }
            return result;
        }
        // 消息预处理
        std::vector<xhqm::byte> md4::preprocess(const std::string& message)
        {
            std::vector<xhqm::byte> result(message.begin(), message.end());
            uint64_t bitLen = result.size() * 8;
            result.push_back(0x80);
            while ((result.size() * 8) % 512 != 448) {
                result.push_back(0);
            }
            for (xhqm::size i = 0; i < 8; i++) {
                result.push_back(static_cast<xhqm::byte>(bitLen >> (i * 8)));
            }
            return result;
        }

        // 十六进制格式化
        std::string md4::formathex(uint32_t num)
        {
            std::ostringstream oss;
            oss << std::hex << std::setfill('0');
            for (int i = 0; i < 4; i++) {
                oss << std::setw(2) << ((num >> (i * 8)) & 0xFF);
            }
            return oss.str();
        }

        xhqm::cipher::md4 md4::hash(const std::vector<xhqm::byte>& data)
        {
            // 重置初始值
            a0 = 0x67452301;
            b0 = 0xEFCDAB89;
            c0 = 0x98BADCFE;
            d0 = 0x10325476;

            for (xhqm::size i = 0; i < data.size(); i += 64) {
                // 将512位分组划分为16个32位字（小端序）
                uint32_t M[16];
                for (xhqm::size j = 0; j < 16; j++) {
                    M[j] = 
                        (static_cast<uint32_t>(data[i + j * 4] + 0) << 0) |
                        (static_cast<uint32_t>(data[i + j * 4 + 1]) << 8) |
                        (static_cast<uint32_t>(data[i + j * 4 + 2]) << 16)|
                        (static_cast<uint32_t>(data[i + j * 4 + 3]) << 24);
                }

                uint32_t A = a0;
                uint32_t B = b0;
                uint32_t C = c0;
                uint32_t D = d0;


                const uint32_t s1[4] = { 3, 7, 11, 19 };
                const uint32_t s2[4] = { 3, 5, 9, 13 };
                const uint32_t s3[4] = { 3, 9, 11, 15 };
                const int k_map_1[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
                const int k_map_2[16] = { 0, 4, 8, 12, 1, 5, 9, 13, 2, 6, 10, 14, 3, 7, 11, 15 };
                const int k_map_3[16] = { 0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15 };

                // 第一轮运算 (16 steps)
                for (int j = 0; j < 16; j++) {
                    uint32_t s = s1[j % 4];;
                    uint32_t k = k_map_1[j];
                    uint32_t temp = A + F(B, C, D) + M[k];
                    A = D;
                    D = C;
                    C = B;
                    B = left(temp, s);
                }

                // 第二轮运算 (16 steps)
                for (int j = 0; j < 16; j++) 
                {
                    uint32_t s = s2[j % 4];
                    uint32_t k = k_map_2[j];
                    uint32_t temp = A + G(B, C, D) + M[k] + 0x5A827999;
                    A = D;
                    D = C;
                    C = B;
                    B = left(temp, s);
                }

                // 第三轮运算 (16 steps)
                for (int j = 0; j < 16; j++) 
                {
                    uint32_t s = s3[j % 4];
                    uint32_t k = k_map_3[j];
                    uint32_t temp = A + H(B, C, D) + M[k] + 0x6ED9EBA1;
                    A = D;
                    D = C;
                    C = B;
                    B = left(temp, s);
                }

                // 更新状态
                a0 += A;
                b0 += B;
                c0 += C;
                d0 += D;
            }

            return *this;
        }
    }
}