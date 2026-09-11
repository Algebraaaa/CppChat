#include "LogicSystem.h"
#include <ostream>
#include <utility>

#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/core/ostream.hpp>
#include <boost/beast/http.hpp>
#include <json/reader.h>
#include <json/value.h>

#include "const.h"
#include "HttpConnection.h"
#include "VerifyGrpcClient.h"
#include "StatusGrpcClient.h"
#include "RedisMgr.h"
#include "Logger.h"
#include "MysqlMgr.h"

namespace beast = boost::beast;
namespace http = boost::beast::http;
// LogicSystem 在当前项目里的真实角色是：
// 保存“URL路径 → 处理函数”的对应关系，然后根据客户端访问的URL，找到并执行对应的处理函数。
LogicSystem::LogicSystem() {
	// 注册get请求
	RegGet("/get_test", [](std::shared_ptr<HttpConnection> connection) {
		beast::ostream(connection->_response.body()) << "receive get_test req " << std::endl;
		int i = 0;
		for (auto& elem : connection->_get_params) {
			i++;
			// 写进_response的HTTP响应正文缓冲区。
			beast::ostream(connection->_response.body()) << "param" << i << " key is " << elem.first;
			beast::ostream(connection->_response.body()) << ", " << " value is " << elem.second << std::endl;
		}

		connection->_response.set(http::field::content_type, "text/plain");
		});
	RegPost("/get_verifycode", [](std::shared_ptr<HttpConnection> connection) {
		// 这个connection里保存着当前客户端的：HTTP请求_request、HTTP响应 _response、客户端Socket、请求正文、URL参数
		// HTTP正文保存在：connection->_request.body()
		auto body_str = boost::beast::buffers_to_string(connection->_request.body().data());
		LOG_DEBUG(
			"POST /get_verifycode received, body bytes=", body_str.size());
		connection->_response.set(http::field::content_type, "text/json");

		// 客户端发来的JSON
		Json::Value request_json;
		// 服务器返回的JSON
		Json::Value response_json;
		// 创建JSON解析器
		Json::Reader reader;

		bool parse_success = reader.parse(body_str, request_json);
		if (!parse_success) {
			LOG_WARNING(
				"POST /get_verifycode contains invalid JSON.");
			response_json["error"] = ErrorCodes::Error_Json;
			std::string jsonstr = response_json.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;
		}
		if (!request_json.isMember("email")) {
			LOG_WARNING(
				"POST /get_verifycode is missing the email field.");
			response_json["error"] = ErrorCodes::Error_Json;
			std::string jsonstr = response_json.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;
		}
		auto email = request_json["email"].asString();
		message::GetVerifyRsp rsp = VerifyGrpcClient::GetInstance()->GetVerifyCode(email);
		LOG_INFO(
			"Verification code request completed, result=", rsp.error());
		response_json["error"] = rsp.error();
		response_json["email"] = request_json["email"];
		std::string jsonstr = response_json.toStyledString();
		beast::ostream(connection->_response.body()) << jsonstr;
		return true;
		});
	RegPost("/user_register", [](std::shared_ptr<HttpConnection> connection) {
		/*
	 * 用户注册接口的业务顺序：
	 * 1. 读取并解析客户端 JSON；
	 * 2. 检查五个必填字段、字段长度和两次密码；
	 * 3. 使用 email 组成 Redis 键，读取并比较邮箱验证码；
	 * 4. 调用 MysqlMgr，把用户写入 MySQL；
	 * 5. 注册成功后删除一次性验证码；
	 * 6. 只返回 uid、user、email 和错误码，不返回密码或验证码。
	 *
	 * 任意一步失败都会立即写入错误响应并 return，不继续执行后续步骤。
	 */
	 // Beast 的请求正文是缓冲区序列，先转换成 std::string 才能交给 JsonCpp。
		auto body_str = boost::beast::buffers_to_string(connection->_request.body().data());

		// 日志只记录正文长度，不记录正文内容，避免把密码和验证码写进日志文件。
		LOG_DEBUG(
			"POST /user_register received, body bytes=", body_str.size());

		// 告诉客户端：当前 HTTP 响应正文使用 JSON 格式。
		connection->_response.set(http::field::content_type, "text/json");

		// request_json 保存客户端请求；response_json 用来组织服务器响应；reader 负责解析字符串。
		Json::Value request_json;
		Json::Value response_json;
		Json::Reader reader;

		// parse 成功后，request_json 才包含 user/email/passwd 等字段。
		bool parse_success = reader.parse(body_str, request_json);
		if (!parse_success) {
			LOG_WARNING(
				"POST /user_register contains invalid JSON.");
			response_json["error"] = ErrorCodes::Error_Json;
			std::string jsonstr = response_json.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;
		}

		// 注册协议规定下面五个字段全部必填，而且都必须是非空字符串。
		// 用数组和循环统一校验，避免为每个字段复制一遍 if。
		const char* required_fields[] = {
			"user", "email", "passwd", "confirm", "verifycode"
		};
		for (const char* field : required_fields) {
			// isMember：字段存在；isString：类型正确；empty：字符串不是空值。
			if (!request_json.isMember(field) || !request_json[field].isString() ||
				request_json[field].asString().empty()) {
				LOG_WARNING(
					"POST /user_register has a missing, empty or non-string field: ",
					field);
				response_json["error"] = ErrorCodes::Error_Json;
				beast::ostream(connection->_response.body()) << response_json.toStyledString();
				return true;
			}
		}

		// 统一校验完成后，再把 JSON 字段转换为带明确含义的局部变量。
		// const 表示注册过程中不会意外改写这些用户输入。
		const std::string name = request_json["user"].asString();
		const std::string email = request_json["email"].asString();
		const std::string password = request_json["passwd"].asString();
		const std::string confirm_password = request_json["confirm"].asString();
		const std::string requested_verify_code = request_json["verifycode"].asString();

		// 长度上限与数据库 VARCHAR 字段以及服务器约定保持一致。
		// 提前拒绝超长输入，比等 MySQL 报“字段过长”更容易控制返回结果。
		if (name.size() > 64 || email.size() > 255 || password.size() > 128) {
			LOG_WARNING("User registration failed: one or more fields are too long.");
			response_json["error"] = ErrorCodes::Error_Json;
			beast::ostream(connection->_response.body()) << response_json.toStyledString();
			return true;
		}

		// confirm 只用于确认用户没有输错密码，不会传给 MysqlDao，也不会保存。
		if (password != confirm_password) {
			LOG_WARNING("User registration failed: passwords do not match.");
			response_json["error"] = ErrorCodes::PasswdErr;
			beast::ostream(connection->_response.body()) << response_json.toStyledString();
			return true;
		}

		// VerifyServer 以 "code_邮箱" 为键保存验证码，例如 code_user@example.com。
		// GateServer 注册时必须使用完全相同的规则，否则永远查不到刚发送的验证码。
		const std::string verify_code_key = "code_" + email;

		// Get 的第二个参数是输出参数：成功后 verify_code 保存 Redis 中的验证码。
		std::string verify_code;
		bool get_verify = RedisMgr::GetInstance()->Get(verify_code_key, verify_code);
		if (!get_verify) {
			// 查不到通常表示验证码未申请、已过期，或者 Redis 当前不可用。
			LOG_WARNING(
				"User registration failed: verification code expired or was not found.");
			response_json["error"] = ErrorCodes::VerifyExpired;
			std::string jsonstr = response_json.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;
		}

		// Redis 中的正确验证码与客户端提交的验证码必须完全一致。
		if (verify_code != requested_verify_code) {
			LOG_WARNING(
				"User registration failed: verification code does not match.");
			response_json["error"] = ErrorCodes::VerifyCodeErr;
			std::string jsonstr = response_json.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;
		}

		// HTTP 层不写 SQL，只通过 MysqlMgr 调用 DAO。
		// RegUser 返回新 uid；用户名或邮箱重复返回 0；数据库故障返回 -1。
		const int uid = MysqlMgr::GetInstance()->RegUser(name, email, password);
		if (uid == 0) {
			// 0 对应 users 表的 username/email 唯一索引冲突。
			LOG_WARNING("User registration failed: username or email already exists.");
			response_json["error"] = ErrorCodes::UserExist;
			beast::ostream(connection->_response.body()) << response_json.toStyledString();
			return true;
		}
		if (uid < 0) {
			// -1 是数据库类错误，不能伪装成 UserExist，否则客户端会得到错误提示。
			LOG_ERROR("User registration failed because MySQL is unavailable or returned an error.");
			response_json["error"] = ErrorCodes::DatabaseError;
			beast::ostream(connection->_response.body()) << response_json.toStyledString();
			return true;
		}

		// 注册成功后验证码立即失效，防止同一验证码被重复用于创建账号。
		// 删除失败只记日志，不回滚已经写入的用户：MySQL 注册已经成功是主结果。
		if (!RedisMgr::GetInstance()->Del(verify_code_key)) {
			LOG_WARNING("User was registered, but the verification code could not be deleted.");
		}

		LOG_INFO("User registration completed: uid=", uid);

		// 组织成功响应。ErrorCodes::Success 的值是 0。
		response_json["error"] = ErrorCodes::Success;
		response_json["uid"] = uid;
		response_json["email"] = email;
		response_json["user"] = name;
		// 密码、确认密码和验证码属于敏感信息，绝不能放回 HTTP 响应。
		// toStyledString 把 Json::Value 序列化成字符串，再写入 Beast 响应正文。
		std::string jsonstr = response_json.toStyledString();
		beast::ostream(connection->_response.body()) << jsonstr;
		return true;
		});
	RegPost("/reset_pwd", [](std::shared_ptr<HttpConnection> connection) {
		auto body_str = boost::beast::buffers_to_string(connection->_request.body().data());
		// 请求正文中包含新密码和验证码，因此日志只记录字节数，不能打印 body_str。
		LOG_DEBUG("POST /reset_pwd received, body bytes=", body_str.size());
		connection->_response.set(http::field::content_type, "text/json");

		// 命名与 /user_register 保持一致：request_json 表示请求，response_json 表示响应。
		Json::Value request_json;
		Json::Value response_json;
		Json::Reader reader;
		bool parse_success = reader.parse(body_str, request_json);
		if (!parse_success) {
			LOG_WARNING("POST /reset_pwd contains invalid JSON.");
			response_json["error"] = ErrorCodes::Error_Json;
			std::string jsonstr = response_json.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;
		}

		// 重置密码协议要求这四个字段都存在、类型为字符串且内容非空。
		const char* required_fields[] = {
			"user", "email", "passwd", "verifycode"
		};
		for (const char* field : required_fields) {
			if (!request_json.isMember(field) || !request_json[field].isString() ||
				request_json[field].asString().empty()) {
				LOG_WARNING(
					"POST /reset_pwd has a missing, empty or non-string field: ",
					field);
				response_json["error"] = ErrorCodes::Error_Json;
				beast::ostream(connection->_response.body()) << response_json.toStyledString();
				return true;
			}
		}

		const std::string email = request_json["email"].asString();
		const std::string name = request_json["user"].asString();
		const std::string password = request_json["passwd"].asString();
		const std::string requested_verify_code = request_json["verifycode"].asString();

		if (name.size() > 64 || email.size() > 255 || password.size() > 128) {
			LOG_WARNING("Password reset failed: one or more fields are too long.");
			response_json["error"] = ErrorCodes::Error_Json;
			beast::ostream(connection->_response.body()) << response_json.toStyledString();
			return true;
		}

		// VerifyServer 和注册接口都使用 "code_邮箱" 作为 Redis 键。
		// 原来的 CODEPREFIX 没有定义，会造成编译错误；这里直接采用项目现有的统一规则。
		const std::string verify_code_key = "code_" + email;
		std::string verify_code;
		bool get_verify = RedisMgr::GetInstance()->Get(verify_code_key, verify_code);
		if (!get_verify) {
			LOG_WARNING("Password reset failed: verification code expired or was not found.");
			response_json["error"] = ErrorCodes::VerifyExpired;
			std::string jsonstr = response_json.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;
		}

		if (verify_code != requested_verify_code) {
			LOG_WARNING("Password reset failed: verification code does not match.");
			response_json["error"] = ErrorCodes::VerifyCodeErr;
			std::string jsonstr = response_json.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;
		}

		// 查询 users 表，确认用户名和邮箱属于同一个账号。
		bool email_valid = MysqlMgr::GetInstance()->CheckEmail(name, email);
		if (!email_valid) {
			LOG_WARNING("Password reset failed: username and email do not match.");
			response_json["error"] = ErrorCodes::EmailNotMatch;
			std::string jsonstr = response_json.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;
		}

		// MysqlDao 会为新密码生成新的随机盐和 PBKDF2 哈希，不会保存明文密码。
		bool update_password = MysqlMgr::GetInstance()->UpdatePwd(name, password);
		if (!update_password) {
			LOG_ERROR("Password reset failed while updating MySQL.");
			response_json["error"] = ErrorCodes::PasswdUpFailed;
			std::string jsonstr = response_json.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;
		}

		// 重置成功后验证码只能使用一次。删除失败不会回滚已经更新的密码。
		if (!RedisMgr::GetInstance()->Del(verify_code_key)) {
			LOG_WARNING("Password was reset, but the verification code could not be deleted.");
		}

		LOG_INFO("Password reset completed.");
		response_json["error"] = ErrorCodes::Success;
		response_json["email"] = email;
		response_json["user"] = name;
		// 新密码和验证码都属于敏感信息，不能写日志，也不能放回 HTTP 响应。
		std::string jsonstr = response_json.toStyledString();
		beast::ostream(connection->_response.body()) << jsonstr;
		return true;
		});
	RegPost("/user_login", [](std::shared_ptr<HttpConnection> connection) {
		// 用户登录请求包含明文密码，所以日志只能记录请求大小，不能打印完整正文。
		auto body_str = boost::beast::buffers_to_string(connection->_request.body().data());
		LOG_DEBUG("POST /user_login received, body bytes=", body_str.size());
		connection->_response.set(http::field::content_type, "text/json");
		Json::Value request_json;
		Json::Value response_json;
		Json::Reader reader;
		bool parse_success = reader.parse(body_str, request_json);
		if (!parse_success) {
			LOG_WARNING("POST /user_login contains invalid JSON.");
			response_json["error"] = ErrorCodes::Error_Json;
			std::string jsonstr = response_json.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;
		}

		const char* required_fields[] = { "email", "passwd" };
		for (const char* field : required_fields) {
			if (!request_json.isMember(field) || !request_json[field].isString() ||
				request_json[field].asString().empty()) {
				LOG_WARNING(
					"POST /user_login has a missing, empty or non-string field: ",
					field);
				response_json["error"] = ErrorCodes::Error_Json;
				beast::ostream(connection->_response.body()) << response_json.toStyledString();
				return true;
			}
		}

		const std::string email = request_json["email"].asString();
		const std::string password = request_json["passwd"].asString();
		if (email.size() > 255 || password.size() > 128) {
			LOG_WARNING("User login failed: one or more fields are too long.");
			response_json["error"] = ErrorCodes::Error_Json;
			beast::ostream(connection->_response.body()) << response_json.toStyledString();
			return true;
		}

		UserInfo user_info;
		// MysqlDao 使用 users 表中的 salt 和 PBKDF2 哈希验证密码，不读取明文密码。
		bool password_valid = MysqlMgr::GetInstance()->CheckPwdByEmail(email, password, user_info);
		if (!password_valid) {
			LOG_WARNING("User login failed: email or password is invalid.");
			response_json["error"] = ErrorCodes::PasswdInvalid;
			std::string jsonstr = response_json.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;
		}

		// 密码验证成功后，请求 StatusServer 分配聊天服务器和登录 token。
		auto status_reply = StatusGrpcClient::GetInstance()->GetChatServer(user_info.uid);
		if (status_reply.error() != ErrorCodes::Success) {
			LOG_WARNING(
				"User login failed while requesting StatusServer: error=",
				status_reply.error());
			// RPC 失败时 StatusGrpcClient 已填入 RPCFailed；业务错误则保留服务端错误码。
			response_json["error"] = status_reply.error();
			std::string jsonstr = response_json.toStyledString();
			beast::ostream(connection->_response.body()) << jsonstr;
			return true;
		}

		LOG_INFO("User login completed: uid=", user_info.uid);
		response_json["error"] = ErrorCodes::Success;
		response_json["user"] = user_info.name;
		response_json["email"] = email;
		response_json["uid"] = user_info.uid;
		response_json["token"] = status_reply.token();
		response_json["host"] = status_reply.host();
		response_json["port"] = status_reply.port();
		// token 属于登录凭据，只返回给客户端，不能写入日志
		std::string jsonstr = response_json.toStyledString();
		beast::ostream(connection->_response.body()) << jsonstr;
		return true;
		});
}

void LogicSystem::RegGet(std::string url, HttpHandler handler) {
	_get_handlers.insert(std::make_pair(url, handler));
}
void LogicSystem::RegPost(std::string url, HttpHandler handler) {
	_post_handlers.insert(std::make_pair(url, handler));
}
LogicSystem::~LogicSystem() {}

bool LogicSystem::HandleGet(std::string path, std::shared_ptr<HttpConnection> con)
{
	// 在 _get_handlers 中查找键等于 path 的元素。
	// 如果没有找到，返回.end
	// end() 不是最后一个元素，而是“所有元素之后的一个无效位置”，专门用来表示没找到或遍历结束
	auto it = _get_handlers.find(path);

	if (it == _get_handlers.end())
	{
		return false;
	}

	it->second(con);
	return true;
}
bool LogicSystem::HandlePost(std::string path, std::shared_ptr<HttpConnection> con)
{
	auto it = _post_handlers.find(path);

	if (it == _post_handlers.end())
	{
		return false;
	}

	it->second(con);
	return true;
}
