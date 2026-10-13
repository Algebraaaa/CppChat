#include "BusinessExecutor.h"
#include "ContactTags.h"
#include "RequestContract.h"
#include "ResourceProtocol.h"
#include "ServicePolicy.h"
#include <atomic>
#include <future>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <stdexcept>

class RequestBoundary : public testing::TestWithParam<int> {};
TEST_P(RequestBoundary, CorrelationIdentifier)
{
  Json::Value request, parsed, response;
  const int length = GetParam();
  request["request_id"] = std::string(length, 'x');
  const bool valid = length >= 1 && length <= 64;
  EXPECT_EQ(RequestContract::ParseRequest(SerializeContactTags(request), parsed), valid);
  RequestContract::EchoId(request, response);
  EXPECT_EQ(response.isMember("request_id"), valid);
}
INSTANTIATE_TEST_SUITE_P(Protocol, RequestBoundary, testing::Values(0, 1, 63, 64, 65, 1024));
TEST(RequestContract, LegacyAndMalformed)
{
  Json::Value request;
  EXPECT_TRUE(RequestContract::ParseRequest("{}", request));
  for (const auto *text : {"[1]", "null", "{", "{\"request_id\":1}"})
    EXPECT_FALSE(RequestContract::ParseRequest(text, request)) << text;
}
class TagBoundary : public testing::TestWithParam<int> {};
TEST_P(TagBoundary, Utf8ByteLimit)
{
  Json::Value input(Json::arrayValue), result;
  input.append(std::string(GetParam(), 'x'));
  EXPECT_EQ(NormalizeContactTags(input, result), GetParam() > 0 && GetParam() <= 128);
}
INSTANTIATE_TEST_SUITE_P(Contacts, TagBoundary, testing::Values(0, 1, 127, 128, 129));
TEST(ContactTags, NormalizeDeduplicateAndRejectControls)
{
  Json::Value input(Json::arrayValue), result;
  input.append("  work  ");
  input.append("work");
  input.append("中文");
  ASSERT_TRUE(NormalizeContactTags(input, result));
  ASSERT_EQ(result.size(), 2u);
  EXPECT_EQ(result[0].asString(), "work");
  EXPECT_EQ(SerializeContactTags(ParseContactTags(SerializeContactTags(result))),
            SerializeContactTags(result));
  input.append("a\nb");
  EXPECT_FALSE(NormalizeContactTags(input, result));
  input = Json::Value(Json::arrayValue);
  for (int i = 0; i < 13; ++i)
    input.append(std::to_string(i));
  EXPECT_FALSE(NormalizeContactTags(input, result));
  EXPECT_FALSE(NormalizeContactTags(Json::Value(12), result));
}
TEST(BusinessExecutor, SameConnectionFifoAndExceptionIsolation)
{
  BusinessExecutor executor(2, 32);
  std::vector<int> sequence;
  testing::MockFunction<void(int)> completed;
  {
    testing::InSequence order;
    EXPECT_CALL(completed, Call(1));
    EXPECT_CALL(completed, Call(2));
  }
  ASSERT_TRUE(executor.Submit("stable", [&] {
    sequence.push_back(1);
    completed.Call(1);
  }));
  ASSERT_TRUE(executor.Submit("stable", [] { throw std::runtime_error("task"); }));
  ASSERT_TRUE(executor.Submit("stable", [&] {
    sequence.push_back(2);
    completed.Call(2);
  }));
  ASSERT_TRUE(executor.WaitIdleUntil(std::chrono::steady_clock::now() + std::chrono::seconds(3)));
  EXPECT_THAT(sequence, testing::ElementsAre(1, 2));
  EXPECT_EQ(executor.Snapshot().failed, 1u);
  executor.BeginStop();
  EXPECT_FALSE(executor.Submit("stable", [] {}));
  executor.Stop();
}
TEST(BusinessExecutor, BoundedAdmissionAndDrain)
{
  BusinessExecutor executor(1, 1);
  std::promise<void> started, release;
  auto gate = release.get_future().share();
  ASSERT_TRUE(executor.Submit("one", [&] {
    started.set_value();
    gate.wait();
  }));
  const bool running =
      started.get_future().wait_for(std::chrono::seconds(3)) == std::future_status::ready;
  if (!running) {
    release.set_value();
    FAIL() << "Worker did not start";
  }
  const bool admitted = executor.Submit("one", [] {});
  const bool rejected = !executor.Submit("one", [] {});
  release.set_value();
  EXPECT_TRUE(admitted);
  EXPECT_TRUE(rejected);
  executor.Stop(true);
  EXPECT_EQ(executor.Snapshot().queued, 0u);
  EXPECT_EQ(executor.Snapshot().active, 0u);
}
TEST(BusinessExecutor, InvalidPolicyAndEmptyTasks)
{
  EXPECT_THROW(BusinessExecutor(0, 1), std::invalid_argument);
  EXPECT_THROW(BusinessExecutor(17, 1), std::invalid_argument);
  EXPECT_THROW(BusinessExecutor(1, 0), std::invalid_argument);
  BusinessExecutor executor(1, 2);
  EXPECT_FALSE(executor.Submit("one", {}));
  EXPECT_TRUE(executor.WaitIdleUntil(std::chrono::steady_clock::now()));
}
TEST(BusinessExecutor, DiscardQueuedTasksOnImmediateStop)
{
  BusinessExecutor executor(1, 2);
  std::promise<void> started, release;
  auto gate = release.get_future().share();
  ASSERT_TRUE(executor.Submit("one", [&] {
    started.set_value();
    gate.wait();
  }));
  if (started.get_future().wait_for(std::chrono::seconds(3)) != std::future_status::ready) {
    release.set_value();
    FAIL() << "Worker did not start";
  }
  EXPECT_FALSE(executor.WaitIdleUntil(std::chrono::steady_clock::now()));
  std::atomic<int> called{0};
  EXPECT_TRUE(executor.Submit("one", [&] { ++called; }));
  EXPECT_TRUE(executor.Submit("one", [&] { ++called; }));
  auto stopped = std::async(std::launch::async, [&] { return executor.Stop(false); });
  // The held worker cannot empty the queue; only Stop(false) can discard it.
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (executor.Snapshot().queued != 0 && std::chrono::steady_clock::now() < deadline)
    std::this_thread::yield();
  const bool emptied = executor.Snapshot().queued == 0;
  release.set_value();
  ASSERT_EQ(stopped.wait_for(std::chrono::seconds(3)), std::future_status::ready);
  const auto discarded = stopped.get();
  EXPECT_TRUE(emptied);
  EXPECT_EQ(discarded, 2u);
  EXPECT_EQ(called.load(), 0);
  EXPECT_EQ(executor.Snapshot().queued, 0u);
}
TEST(Policy, DocumentedLimitsAndLeases)
{
  EXPECT_EQ(cppchat::resource::kChunkSize, 32u * 1024);
  EXPECT_EQ(cppchat::resource::kMaximumPacketSize, 60u * 1024);
  EXPECT_EQ(cppchat::resource::kMaximumImageSize, 20u * 1024 * 1024);
  EXPECT_GT(ServicePolicy::ActiveSessionSeconds, ServicePolicy::NodeLeaseSeconds);
}
