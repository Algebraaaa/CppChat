#if defined(TEST_GATE_POOL) || defined(TEST_CHAT_DAO)
#include "MysqlDao.h"
#endif
#include "MySqlPool.h"
#include <chrono>
#include <cppconn/resultset.h>
#include <cppconn/statement.h>
#include <cstdlib>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
void require(bool condition, const char *message)
{
  if (!condition)
    throw std::runtime_error(message);
}
std::string env(const char *key)
{
  const auto *value = std::getenv(key);
  if (!value)
    throw std::runtime_error("Missing test environment");
  return value;
}
int scalar(sql::Connection &connection, const std::string &query)
{
  std::unique_ptr<sql::Statement> statement(connection.createStatement());
  std::unique_ptr<sql::ResultSet> result(statement->executeQuery(query));
  require(result->next(), "Missing scalar result");
  return result->getInt(1);
}
void control(const char *action)
{
  std::cout << "CONTROL:" << action << std::endl;
  std::string reply;
  require(bool(std::getline(std::cin, reply)) && reply == "continue", "Controller failed");
}
} // namespace

int main()
{
  try {
    const auto url = env("MYSQL_TEST_URL"), user = env("MYSQL_TEST_USER");
    const auto password = env("MYSQL_TEST_PASSWORD"), schema = env("MYSQL_TEST_SCHEMA");
    MySqlPoolOptions options;
    options.borrow_timeout = std::chrono::milliseconds(200);
    MySqlPool pool(url, user, password, schema, 1, options,
                   [](const std::string &) { throw std::runtime_error("Diagnostic failure"); });
    auto invalid_options = options;
    invalid_options.read_timeout_seconds = 0;
    bool rejected = false;
    try {
      MySqlPool invalid(url, user, password, schema, 1, invalid_options);
    } catch (const std::invalid_argument &) {
      rejected = true;
    }
    require(rejected, "Invalid timeout policy was accepted");
    auto connection = pool.GetConnection();
    require(bool(connection), "Initial borrow failed");
    const int first_id = scalar(*connection, "SELECT CONNECTION_ID()");
    scalar(*connection, "SELECT 1");
    std::unique_ptr<sql::Statement> statement(connection->createStatement());
    statement->execute("SET SESSION wait_timeout = 1");
    statement.reset();
    pool.ReturnConnection(std::move(connection));
    // The container controller observes server-side expiry, rather than guessing a delay.
    control("idle");
    connection = pool.GetConnection();
    require(bool(connection), "Idle timeout did not recover");
    require(scalar(*connection, "SELECT CONNECTION_ID()") != first_id, "Session was not rebuilt");
    require(scalar(*connection, "SELECT DATABASE() = '" + schema + "'") == 1,
            "Schema was not restored");

    MySqlPool killer_pool(url, user, password, schema, 1);
    auto killer = killer_pool.GetConnection();
    require(bool(killer), "Killer connection failed");
    statement.reset(killer->createStatement());
    statement->execute("KILL " + std::to_string(scalar(*connection, "SELECT CONNECTION_ID()")));
    statement.reset();
    killer.reset();
    pool.ReturnConnection(std::move(connection));
    connection = pool.GetConnection();
    require(connection && scalar(*connection, "SELECT 1") == 1, "Killed session did not recover");
    pool.ReturnConnection(std::move(connection));
#ifdef TEST_GATE_POOL
    MysqlDao dao;
    require(dao.RegUser("RecoveryTest", "recovery@example.test", "Initial123") > 0,
            "Fixture registration failed");
    require(dao.CheckEmail("RecoveryTest", "recovery@example.test") == AccountCheckResult::Match,
            "Valid account rejected");
    require(dao.CheckEmail("OtherUser", "recovery@example.test") == AccountCheckResult::NoMatch,
            "Mismatch accepted");
    UserInfo info;
    require(dao.CheckPwdByEmail("recovery@example.test", "Wrong123", info) ==
                AccountCheckResult::NoMatch,
            "Wrong password accepted");
#elif defined(TEST_CHAT_DAO)
    MysqlDao dao;
    require(dao.RegUser("", "recovery@example.test", "Initial123") < 0, "Empty account accepted");
    const int uid = dao.RegUser("RecoveryTest", "recovery@example.test", "Initial123");
    require(uid > 0, "Shared DAO registration failed");
    require(dao.RegUser("RecoveryTest", "recovery@example.test", "Initial123") == 0,
            "Duplicate shared DAO user accepted");
    require(dao.CheckEmail("RecoveryTest", "recovery@example.test"), "Shared DAO email rejected");
    require(!dao.CheckEmail("Other", "recovery@example.test") && !dao.CheckEmail("", ""),
            "Shared DAO mismatch accepted");
    UserInfo info;
    require(dao.CheckPwdByEmail("recovery@example.test", "Initial123", info),
            "Shared DAO password rejected");
    require(!dao.CheckPwdByEmail("recovery@example.test", "Wrong123", info) &&
                !dao.CheckPwdByEmail("missing@example.test", "Initial123", info) &&
                !dao.CheckPwdByEmail("", "", info),
            "Shared DAO bad credential accepted");
    require(!dao.UpdatePwd("", "Initial123") && !dao.UpdatePwd("missing", "Initial123"),
            "Missing account password update accepted");
    require(dao.UpdatePwd("RecoveryTest", "Initial123"), "Unchanged-password rehash failed");
#endif
    control("block");
    require(!pool.GetConnection(), "Unavailable database yielded a connection");
    require(!pool.GetConnection(), "Repeated outage borrow did not return");
    MySqlPool cold_pool(url, user, password, schema, 1);
    require(!cold_pool.GetConnection(), "Cold-start database failure was not detected");
#ifdef TEST_GATE_POOL
    MysqlDao cold_dao;
    require(dao.CheckEmail("RecoveryTest", "recovery@example.test") ==
                AccountCheckResult::DatabaseError,
            "Outage reported as mismatch");
    require(dao.CheckPwdByEmail("recovery@example.test", "Initial123", info) ==
                AccountCheckResult::DatabaseError,
            "Outage reported as wrong password");
    require(dao.UpdatePwd("RecoveryTest", "Updated123") == PasswordUpdateResult::DatabaseError,
            "Update outage misclassified");
#elif defined(TEST_CHAT_DAO)
    MysqlDao cold_dao;
    require(!cold_dao.CheckEmail("RecoveryTest", "recovery@example.test"),
            "Cold shared DAO accepted outage");
    require(!dao.CheckEmail("RecoveryTest", "recovery@example.test"), "Shared DAO accepted outage");
    require(!dao.CheckPwdByEmail("recovery@example.test", "Initial123", info),
            "Shared DAO password accepted outage");
    require(!dao.UpdatePwd("RecoveryTest", "Updated123"),
            "Shared DAO password updated during outage");
#endif
    control("restore");
    connection = pool.GetConnection();
    require(connection && scalar(*connection, "SELECT 1") == 1,
            "Pool lost its capacity after outage");
    pool.ReturnConnection(std::move(connection));
    connection = cold_pool.GetConnection();
    require(connection && scalar(*connection, "SELECT 1") == 1, "Cold pool did not recover");
    cold_pool.ReturnConnection(std::move(connection));
#ifdef TEST_GATE_POOL
    require(cold_dao.CheckEmail("RecoveryTest", "recovery@example.test") ==
                AccountCheckResult::Match,
            "Cold DAO did not recover");
    require(dao.CheckEmail("RecoveryTest", "recovery@example.test") == AccountCheckResult::Match,
            "DAO failed after recovery");
    require(dao.UpdatePwd("RecoveryTest", "Updated123") == PasswordUpdateResult::Updated,
            "Password update failed after recovery");
    require(dao.CheckPwdByEmail("recovery@example.test", "Updated123", info) ==
                AccountCheckResult::Match,
            "Updated password rejected");
    require(dao.CheckPwdByEmail("recovery@example.test", "Initial123", info) ==
                AccountCheckResult::NoMatch,
            "Old password still accepted");
#elif defined(TEST_CHAT_DAO)
    require(cold_dao.CheckEmail("RecoveryTest", "recovery@example.test"),
            "Cold shared DAO did not recover");
    require(dao.UpdatePwd("RecoveryTest", "Updated123"), "Shared DAO update failed after recovery");
    require(dao.CheckPwdByEmail("recovery@example.test", "Updated123", info) &&
                !dao.CheckPwdByEmail("recovery@example.test", "Initial123", info),
            "Shared DAO password state invalid after recovery");
#endif
    // Concurrent borrowers cannot share a session. A waiter must wake on Close.
    auto held = pool.GetConnection();
    const auto wait_start = std::chrono::steady_clock::now();
    require(!pool.GetConnection(), "Exhausted pool yielded a connection");
    require(std::chrono::steady_clock::now() - wait_start >= options.borrow_timeout,
            "Borrow timeout policy was ignored");
    auto waiter = std::async(std::launch::async, [&pool] { return pool.GetConnection(); });
    require(waiter.wait_for(std::chrono::milliseconds(100)) == std::future_status::timeout,
            "Pool over-issued a session");
    pool.Close();
    require(waiter.wait_for(std::chrono::seconds(1)) == std::future_status::ready && !waiter.get(),
            "Close did not wake waiter");
    pool.ReturnConnection(std::move(held));
    require(!pool.GetConnection(), "Closed pool yielded a connection");
    std::cout << "PASS: idle timeout, killed session, outage/recovery, authentication errors, close"
              << std::endl;
    return 0;
  } catch (const std::exception &exception) {
    std::cerr << "FAIL: " << exception.what() << std::endl;
    return 1;
  }
}
