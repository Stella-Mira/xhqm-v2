// network 测试：md5 / base64 / uuid（纯算法部分）
#include "../network/md5.h"
#include "../network/base64.h"
#include "../network/uuid.h"
#include "test.h"
#include <string>
#include <vector>

using namespace xhqm;

TEST_CASE("test_md5")
{
	// 标准测试向量
	CHECK(std::string(cipher::md5()("")) == "d41d8cd98f00b204e9800998ecf8427e");
	CHECK(std::string(cipher::md5()("hello")) == "5d41402abc4b2a76b9719d911017c592");
	CHECK(std::string(cipher::md5()("abc")) == "900150983cd24fb0d6963f7d28e17f72");
}

TEST_CASE("test_base64")
{
	// 标准测试向量
	CHECK(cipher::base64::encode_text("hello") == "aGVsbG8=");
	CHECK(cipher::base64::encode_text("") == "");
	CHECK(cipher::base64::encode_text("a") == "YQ==");
	CHECK(cipher::base64::encode_text("ab") == "YWI=");
	CHECK(cipher::base64::encode_text("abc") == "YWJj");

	// 往返
	const std::string s = "xhqm base64 round trip test 123456";
	CHECK(cipher::base64::decode_text(cipher::base64::encode_text(s)) == s);
}

TEST_CASE("test_uuid")
{
	auto u = cipher::uuid::creat_uuid_v4();
	const std::string t = cipher::uuid::to_text(u);
	CHECK(t.size() == 36);   // 8-4-4-4-12 格式

	// 版本位 = 4（v4）
	CHECK(t[14] == '4');

	// 变体位（第 19 位是 8/9/a/b）
	const char variant = t[19];
	CHECK((variant == '8' || variant == '9' || variant == 'a' || variant == 'b' || variant == 'A' || variant == 'B'));

	// 往返
	auto u2 = cipher::uuid::to_uuid(t);
	CHECK(cipher::uuid::to_text(u2) == t);

	// 两次生成不同
	auto u3 = cipher::uuid::creat_uuid_v4();
	CHECK(cipher::uuid::to_text(u3) != t);
}
