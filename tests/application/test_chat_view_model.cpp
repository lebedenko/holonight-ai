#include "credentials/fake_credential_store.h"
#include "holonight_application/chat_view_model.h"
#include "holonight_application/conversation_list_model.h"
#include "holonight_application/message_list_model.h"
#include "holonight_application/provider_runtime_coordinator.h"
#include "holonight_application/tools/i_tool.h"
#include "holonight_application/tools/tool_presenters.h"
#include "holonight_application/tools/tool_registry.h"
#include "holonight_providers/anthropic_provider.h"
#include "holonight_providers/google_provider.h"
#include "holonight_providers/ollama_provider.h"
#include "holonight_providers/openai_provider.h"
#include "persistence/fake_conversation_repository.h"
#include "providers/fake_http_client.h"
#include "testing/scoped_message_capture.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QString>
#include <QUuid>
#include <QVariantList>
#include <QVariantMap>

#include <gtest/gtest.h>
#include <holonight_config/utility_config.h>
#include <memory>
#include <utility>

namespace holonight_application {
namespace {

using holonight_persistence::FakeConversationRepository;
using holonight_providers::AnthropicProvider;
using holonight_providers::FakeHttpClient;
using holonight_providers::GoogleProvider;
using holonight_providers::OllamaProvider;
using holonight_providers::OpenAIProvider;
using holonight_testing::ScopedMessageCapture;

std::shared_ptr<OllamaProvider> makeProviderWithModel(const std::shared_ptr<FakeHttpClient>& client) {
  client->enqueueBufferedSuccess(QByteArray(R"({"models":[{"name":"llama3"}]})"));
  return std::make_shared<OllamaProvider>(client);
}

std::shared_ptr<OllamaProvider> makeProviderWithNoModels(const std::shared_ptr<FakeHttpClient>& client) {
  // No buffered response is queued, so ChatViewModel's initial refresh leaves the list empty.
  return std::make_shared<OllamaProvider>(client);
}

// These tests exercise only Ollama-model code paths; the other providers remain empty.
std::shared_ptr<OpenAIProvider> makeEmptyOpenAiProvider() {
  return std::make_shared<OpenAIProvider>(std::make_shared<FakeHttpClient>());
}

std::shared_ptr<OpenAIProvider> makeOpenAiProviderWithModel(const std::shared_ptr<FakeHttpClient>& client) {
  client->enqueueBufferedSuccess(QByteArray(R"({"data":[{"id":"gpt-4o"}]})"));
  auto provider = std::make_shared<OpenAIProvider>(client);
  provider->refresh();
  return provider;
}

// These tests exercise only Ollama/OpenAI-model code paths.
std::shared_ptr<AnthropicProvider> makeEmptyAnthropicProvider() {
  return std::make_shared<AnthropicProvider>(std::make_shared<FakeHttpClient>());
}

std::shared_ptr<AnthropicProvider> makeAnthropicProviderWithModel(const std::shared_ptr<FakeHttpClient>& client) {
  client->enqueueBufferedSuccess(QByteArray(R"({"data":[{"id":"claude-3-5-sonnet-20241022"}]})"));
  auto provider = std::make_shared<AnthropicProvider>(client);
  provider->refresh();
  return provider;
}

// REQ-F-001/REQ-F-007/REQ-F-011 (T-011): a test-only ITool exercising ChatViewModel's own
// tool-call row-sync logic (syncNewMessagesIntoModel()) end to end, independent of any concrete
// tool's real behavior.
class RecordingTool : public ITool {
 public:
  [[nodiscard]] QString name() const override { return QStringLiteral("RecordingTool"); }
  [[nodiscard]] QString description() const override { return QStringLiteral("Test-only no-op tool."); }
  [[nodiscard]] QJsonObject schema() const override { return QJsonObject{}; }
  [[nodiscard]] QJsonObject execute(const QJsonObject& /*parameters*/) override {
    return QJsonObject{{QStringLiteral("ok"), true}};
  }
};

class DeferredToolHandle final : public ToolExecutionHandle {
 public:
  [[nodiscard]] bool canCancel() const override { return false; }
  void cancel() override {}
};

class DeferredListFilesExecutor final : public IToolExecutor {
 public:
  [[nodiscard]] ToolExecutionHandlePtr start(const ToolExecutionRequest& request,
                                             ToolOutcomeCallback on_finished) override {
    request_ = request;
    on_finished_ = std::move(on_finished);
    return std::make_shared<DeferredToolHandle>();
  }

  void complete(QJsonValue result) {
    ASSERT_TRUE(static_cast<bool>(on_finished_));
    ToolOutcomeCallback callback = std::exchange(on_finished_, {});
    callback(ToolOutcome{.result = std::move(result)});
  }

  [[nodiscard]] const ToolExecutionRequest& request() const { return request_; }

 private:
  ToolExecutionRequest request_;
  ToolOutcomeCallback on_finished_;
};

// Mirrors test_chat_controller.cpp's toolCallRoundSse(): the minimal Anthropic SSE sequence
// yielding exactly one StreamEvent::ToolCall.
QByteArray toolCallRoundSse(const QString& toolUseId) {
  return QByteArray(R"(data: {"type":"content_block_start","index":0,"content_block":{"type":"tool_use","id":")") +
         toolUseId.toUtf8() + QByteArray(R"(","name":"RecordingTool"}})") + "\n\n" +
         QByteArray(R"(data: {"type":"content_block_stop","index":0})") + "\n\n" +
         QByteArray(R"(data: {"type":"message_stop"})") + "\n\n";
}

QByteArray listFilesToolCallRoundSse(const QString& toolUseId) {
  return QByteArray(R"(data: {"type":"content_block_start","index":0,"content_block":{"type":"tool_use","id":")") +
         toolUseId.toUtf8() + QByteArray(R"(","name":"list_files"}})") + "\n\n" +
         QByteArray(
             R"(data: {"type":"content_block_delta","index":0,"delta":{"type":"input_json_delta","partial_json":"{\"path\":\"~/Pictures\"}"}})") +
         "\n\n" + QByteArray(R"(data: {"type":"content_block_stop","index":0})") + "\n\n" +
         QByteArray(R"(data: {"type":"message_stop"})") + "\n\n";
}

QVariantMap toolCallAt(MessageListModel& model, int row) {
  return model.data(model.index(row), MessageListModel::ToolCallRole).toMap();
}

// These tests exercise only Ollama/OpenAI/Anthropic-model code paths.
std::shared_ptr<GoogleProvider> makeEmptyGoogleProvider() {
  return std::make_shared<GoogleProvider>(std::make_shared<FakeHttpClient>());
}

struct PopulatedFixture {
  std::shared_ptr<FakeHttpClient> http_client = std::make_shared<FakeHttpClient>();
  std::shared_ptr<OllamaProvider> provider = makeProviderWithModel(http_client);
  std::shared_ptr<OpenAIProvider> openai_provider = makeEmptyOpenAiProvider();
  std::shared_ptr<AnthropicProvider> anthropic_provider = makeEmptyAnthropicProvider();
  std::shared_ptr<GoogleProvider> google_provider = makeEmptyGoogleProvider();
  std::unique_ptr<FakeConversationRepository> repository_owner = std::make_unique<FakeConversationRepository>();
  FakeConversationRepository* repository = repository_owner.get();
  ChatViewModel view_model{provider, openai_provider, anthropic_provider, google_provider, std::move(repository_owner)};
};

struct EmptyFixture {
  std::shared_ptr<FakeHttpClient> http_client = std::make_shared<FakeHttpClient>();
  std::shared_ptr<OllamaProvider> provider = makeProviderWithNoModels(http_client);
  std::shared_ptr<OpenAIProvider> openai_provider = makeEmptyOpenAiProvider();
  std::shared_ptr<AnthropicProvider> anthropic_provider = makeEmptyAnthropicProvider();
  std::shared_ptr<GoogleProvider> google_provider = makeEmptyGoogleProvider();
  std::unique_ptr<FakeConversationRepository> repository_owner = std::make_unique<FakeConversationRepository>();
  FakeConversationRepository* repository = repository_owner.get();
  ChatViewModel view_model{provider, openai_provider, anthropic_provider, google_provider, std::move(repository_owner)};
};

QString textAt(MessageListModel& model, int row) {
  return model.data(model.index(row), MessageListModel::TextRole).toString();
}

QString statusAt(MessageListModel& model, int row) {
  return model.data(model.index(row), MessageListModel::StatusRole).toString();
}

QDateTime createdAtAt(MessageListModel& model, int row) {
  return model.data(model.index(row), MessageListModel::CreatedAtRole).toDateTime();
}

QString modelNameAt(MessageListModel& model, int row) {
  return model.data(model.index(row), MessageListModel::ModelNameRole).toString();
}

QString conversationIdAt(ConversationListModel& model, int row) {
  return model.data(model.index(row), ConversationListModel::IdRole).toString();
}

bool conversationPinnedAt(ConversationListModel& model, int row) {
  return model.data(model.index(row), ConversationListModel::PinnedRole).toBool();
}

// T-015: Initialization and model population flow (REQ-F-001, REQ-F-002, REQ-F-004).
TEST(ChatViewModel, ConstructionCreatesStableEphemeralConversation) {
  PopulatedFixture fixture;

  auto* conversation = fixture.view_model.conversation();
  ASSERT_NE(conversation, nullptr);
  EXPECT_FALSE(conversation->id().toString().isEmpty());
  EXPECT_EQ(fixture.view_model.conversation(), conversation);
  EXPECT_TRUE(fixture.view_model.messagesReady());
}

TEST(ChatViewModel, ConstructionPreparesEveryRegisteredProvider) {
  auto credentialStore = std::make_unique<holonight_credentials::FakeCredentialStore>();
  ChatViewModel viewModel{makeProviderWithNoModels(std::make_shared<FakeHttpClient>()),
                          makeEmptyOpenAiProvider(),
                          makeEmptyAnthropicProvider(),
                          makeEmptyGoogleProvider(),
                          std::make_unique<FakeConversationRepository>(),
                          {},
                          nullptr,
                          std::move(credentialStore)};

  auto* coordinator = viewModel.providerRuntimeCoordinator();
  ASSERT_NE(coordinator, nullptr);
  EXPECT_EQ(coordinator->readiness(QStringLiteral("ollama")), ProviderReadiness::Ready);
  EXPECT_EQ(coordinator->readiness(QStringLiteral("openai")), ProviderReadiness::MissingCredential);
  EXPECT_EQ(coordinator->readiness(QStringLiteral("anthropic")), ProviderReadiness::MissingCredential);
  EXPECT_EQ(coordinator->readiness(QStringLiteral("google")), ProviderReadiness::MissingCredential);
}

TEST(ChatViewModel, ConstructionPopulatesAvailableModelsAndSelectsFirst) {
  PopulatedFixture fixture;

  EXPECT_EQ(fixture.http_client->bufferedCallCount(), 1U);

  const QStringList models = fixture.view_model.availableModelNames();
  ASSERT_EQ(models.size(), 1);
  EXPECT_EQ(models.front(), QStringLiteral("llama3"));
  EXPECT_EQ(fixture.view_model.selectedProviderId(), QStringLiteral("ollama"));
  EXPECT_EQ(fixture.view_model.selectedModelName(), QStringLiteral("llama3"));
  EXPECT_TRUE(fixture.view_model.canSend());
}

// T-016: Empty model list, send flow, and streaming state (REQ-F-003, REQ-F-005, REQ-F-006,
// REQ-F-007, REQ-F-008, REQ-F-009).
TEST(ChatViewModel, EmptyModelListDisablesSend) {
  EmptyFixture fixture;

  EXPECT_TRUE(fixture.view_model.availableProviders().isEmpty());
  EXPECT_TRUE(fixture.view_model.availableModelNames().isEmpty());
  EXPECT_FALSE(fixture.view_model.canSend());
}

TEST(ChatViewModel, SendWithEmptyOrWhitespaceTextIsRejected) {
  PopulatedFixture fixture;

  fixture.view_model.send(QString());
  EXPECT_EQ(fixture.view_model.messages()->rowCount(), 0);
  EXPECT_FALSE(fixture.view_model.isStreaming());

  fixture.view_model.send(QStringLiteral("   "));
  EXPECT_EQ(fixture.view_model.messages()->rowCount(), 0);
  EXPECT_FALSE(fixture.view_model.isStreaming());
}

TEST(ChatViewModel, SendAppendsMessagesClearsInputAndStartsStreaming) {
  PopulatedFixture fixture;

  fixture.view_model.setInputText(QStringLiteral("hello"));
  fixture.view_model.send(QStringLiteral("hello"));

  ASSERT_EQ(fixture.view_model.messages()->rowCount(), 2);
  EXPECT_TRUE(fixture.view_model.inputText().isEmpty());
  EXPECT_TRUE(fixture.view_model.isStreaming());
  EXPECT_FALSE(fixture.view_model.canSend());
}

TEST(ChatViewModel, MessageModelExposesCreatedAtRole) {
  PopulatedFixture fixture;
  fixture.view_model.send(QStringLiteral("hello"));

  ASSERT_EQ(fixture.view_model.messages()->rowCount(), 2);
  ASSERT_EQ(fixture.view_model.conversation()->messages().size(), 2U);
  EXPECT_EQ(fixture.view_model.messages()->roleNames().value(MessageListModel::CreatedAtRole),
            QByteArrayLiteral("createdAt"));
  EXPECT_EQ(createdAtAt(*fixture.view_model.messages(), 0),
            fixture.view_model.conversation()->messages()[1].createdAt());
}

TEST(ChatViewModel, MessageModelExposesModelNameOnlyForAssistantMessage) {
  PopulatedFixture fixture;
  fixture.view_model.send(QStringLiteral("hello"));

  ASSERT_EQ(fixture.view_model.messages()->rowCount(), 2);
  EXPECT_EQ(fixture.view_model.messages()->roleNames().value(MessageListModel::ModelNameRole),
            QByteArrayLiteral("modelName"));
  EXPECT_EQ(modelNameAt(*fixture.view_model.messages(), 0), QStringLiteral("llama3"));
  EXPECT_TRUE(modelNameAt(*fixture.view_model.messages(), 1).isEmpty());
}

TEST(ChatViewModel, SendIsRejectedWhenCanSendIsFalse) {
  EmptyFixture fixture;

  fixture.view_model.send(QStringLiteral("hello"));

  EXPECT_EQ(fixture.view_model.messages()->rowCount(), 0);
  EXPECT_FALSE(fixture.view_model.isStreaming());
}

TEST(ChatViewModel, ContentDeltaAccumulatesTextInPlaceWithoutChangingRowCount) {
  PopulatedFixture fixture;
  fixture.view_model.send(QStringLiteral("hello"));
  ASSERT_EQ(fixture.http_client->streamingCallCount(), 1U);

  fixture.http_client->emitData(
      0, QByteArray(R"({"model":"llama3","message":{"role":"assistant","content":"Hi"},"done":false})") + "\n");
  EXPECT_EQ(fixture.view_model.messages()->rowCount(), 2);
  EXPECT_EQ(textAt(*fixture.view_model.messages(), 0), QStringLiteral("Hi"));

  fixture.http_client->emitData(
      0, QByteArray(R"({"model":"llama3","message":{"role":"assistant","content":" there"},"done":false})") + "\n");
  EXPECT_EQ(fixture.view_model.messages()->rowCount(), 2);
  EXPECT_EQ(textAt(*fixture.view_model.messages(), 0), QStringLiteral("Hi there"));
}

TEST(ChatViewModel, CompletedStreamFlipsIsStreamingFalseAndMarksMessageComplete) {
  PopulatedFixture fixture;
  fixture.view_model.send(QStringLiteral("hello"));

  fixture.http_client->emitData(
      0, QByteArray(R"({"model":"llama3","message":{"role":"assistant","content":"Hi"},"done":false})") + "\n");
  fixture.http_client->emitData(
      0, QByteArray(R"({"model":"llama3","message":{"role":"assistant","content":""},"done":true})") + "\n");

  EXPECT_FALSE(fixture.view_model.isStreaming());
  EXPECT_EQ(statusAt(*fixture.view_model.messages(), 0), QStringLiteral("complete"));
}

// T-011 (REQ-F-007/REQ-F-011): ChatController appends 3 new Conversation messages (invocation,
// result, next placeholder) around a single ToolCall StreamEvent -- this exercises
// ChatViewModel::syncNewMessagesIntoModel() catching the model's row count up across two HTTP
// rounds, and MessageListModel's ToolCallRole exposing the right structured data for each.
TEST(ChatViewModel, ToolCallRoundTripInsertsInvocationAndResultRowsInOrder) {
  auto anthropicHttpClient = std::make_shared<FakeHttpClient>();
  auto anthropicProvider = makeAnthropicProviderWithModel(anthropicHttpClient);
  auto toolRegistry = std::make_shared<ToolRegistry>();
  toolRegistry->registerTool(std::make_shared<RecordingTool>());
  auto repositoryOwner = std::make_unique<FakeConversationRepository>();
  FakeConversationRepository* repository = repositoryOwner.get();

  ChatViewModel viewModel{makeProviderWithNoModels(std::make_shared<FakeHttpClient>()),
                          makeEmptyOpenAiProvider(),
                          anthropicProvider,
                          makeEmptyGoogleProvider(),
                          std::move(repositoryOwner),
                          {},
                          nullptr,
                          nullptr,
                          {},
                          {},
                          {},
                          nullptr,
                          toolRegistry};
  viewModel.setSelectedProviderId(QStringLiteral("anthropic"));

  viewModel.send(QStringLiteral("hello"));
  ASSERT_EQ(anthropicHttpClient->streamingCallCount(), 1U);
  ASSERT_EQ(viewModel.messages()->rowCount(), 2);

  anthropicHttpClient->emitData(0, toolCallRoundSse(QStringLiteral("toolu_01")));

  EXPECT_EQ(repository->allMessages(viewModel.activeConversationId()).size(), 5U);

  // Invocation and result become visible and persistent at the same synchronization point; they
  // must not depend on the follow-up request producing another event (REQ-F-008/REQ-F-011).
  ASSERT_EQ(anthropicHttpClient->streamingCallCount(), 2U);
  ASSERT_EQ(viewModel.messages()->rowCount(), 4);
  EXPECT_EQ(statusAt(*viewModel.messages(), 0), QStringLiteral("streaming"));
  const QVariantMap invocationCall = toolCallAt(*viewModel.messages(), 1);
  EXPECT_EQ(invocationCall.value(QStringLiteral("kind")).toString(), QStringLiteral("invocation"));
  EXPECT_EQ(invocationCall.value(QStringLiteral("toolName")).toString(), QStringLiteral("RecordingTool"));
  EXPECT_FALSE(invocationCall.value(QStringLiteral("isError")).toBool());
  EXPECT_EQ(repository->persistNewMessageCallCount(), 3);
  EXPECT_EQ(repository->allMessages(viewModel.activeConversationId()).size(), 5U);

  anthropicHttpClient->emitData(
      1, QByteArray(R"(data: {"type":"content_block_delta","delta":{"type":"text_delta","text":"done"}})") + "\n\n" +
             QByteArray(R"(data: {"type":"message_stop"})") + "\n\n");

  // The existing placeholder completes in place; chronological ordering remains unchanged.
  ASSERT_EQ(viewModel.messages()->rowCount(), 4);
  EXPECT_EQ(textAt(*viewModel.messages(), 0), QStringLiteral("done"));
  EXPECT_EQ(statusAt(*viewModel.messages(), 0), QStringLiteral("complete"));
  const QVariantMap invocationCallAfterFollowUp = toolCallAt(*viewModel.messages(), 1);
  EXPECT_EQ(invocationCallAfterFollowUp.value(QStringLiteral("kind")).toString(), QStringLiteral("invocation"));
  EXPECT_FALSE(invocationCallAfterFollowUp.value(QStringLiteral("isError")).toBool());
  EXPECT_FALSE(viewModel.isStreaming());

  // No duplicate inserts occur when the follow-up stream begins.
  EXPECT_EQ(repository->persistNewMessageCallCount(), 3);
  EXPECT_EQ(repository->allMessages(viewModel.activeConversationId()).size(), 5U);
}

TEST(ChatViewModel, AsyncToolActivityPublishesRunningThenCompletesInPlaceAndPersistsBothBoundaries) {
  auto anthropicHttpClient = std::make_shared<FakeHttpClient>();
  auto anthropicProvider = makeAnthropicProviderWithModel(anthropicHttpClient);
  auto executor = std::make_shared<DeferredListFilesExecutor>();
  auto toolRegistry = std::make_shared<ToolRegistry>();
  toolRegistry->registerTool(ToolRegistration{
      .definition = ToolDefinition{.id = QStringLiteral("filesystem.list"),
                                   .function_name = QStringLiteral("list_files"),
                                   .display_name = QStringLiteral("List files"),
                                   .renderer_key = QStringLiteral("filesystem.list"),
                                   .description = QStringLiteral("Lists directory entries."),
                                   .input_schema = QJsonObject{}},
      .executor = executor,
      .presenter = std::make_shared<ListFilesPresenter>(),
  });
  auto repositoryOwner = std::make_unique<FakeConversationRepository>();
  FakeConversationRepository* repository = repositoryOwner.get();

  ChatViewModel viewModel{makeProviderWithNoModels(std::make_shared<FakeHttpClient>()),
                          makeEmptyOpenAiProvider(),
                          anthropicProvider,
                          makeEmptyGoogleProvider(),
                          std::move(repositoryOwner),
                          {},
                          nullptr,
                          nullptr,
                          {},
                          {},
                          {},
                          nullptr,
                          toolRegistry};
  viewModel.setSelectedProviderId(QStringLiteral("anthropic"));
  viewModel.send(QStringLiteral("show pictures"));

  anthropicHttpClient->emitData(0, listFilesToolCallRoundSse(QStringLiteral("toolu_async")));

  ASSERT_EQ(viewModel.messages()->rowCount(), 3);
  // The provider's message_stop arrives before the deferred executor completes. No follow-up
  // request may be sent until the local result is available.
  ASSERT_EQ(anthropicHttpClient->streamingCallCount(), 1U);
  const QVariantMap running = toolCallAt(*viewModel.messages(), 0);
  EXPECT_EQ(running.value(QStringLiteral("status")).toString(), QStringLiteral("running"));
  EXPECT_EQ(running.value(QStringLiteral("rendererKey")).toString(), QStringLiteral("filesystem.list"));
  EXPECT_EQ(running.value(QStringLiteral("canonicalId")).toString(), QStringLiteral("filesystem.list"));
  EXPECT_EQ(running.value(QStringLiteral("toolTitle")).toString(), QStringLiteral("Listing ~/Pictures…"));
  EXPECT_EQ(running.value(QStringLiteral("detailData")).toMap().value(QStringLiteral("path")).toString(),
            QStringLiteral("~/Pictures"));
  EXPECT_FALSE(running.value(QStringLiteral("rawResultAvailable")).toBool());
  EXPECT_EQ(executor->request().parameters.toObject().value(QStringLiteral("path")).toString(),
            QStringLiteral("~/Pictures"));

  const auto persistedRunning = repository->allMessages(viewModel.activeConversationId());
  ASSERT_EQ(persistedRunning.size(), 3U);
  ASSERT_EQ(persistedRunning.back().toolCalls().size(), 1U);
  EXPECT_EQ(persistedRunning.back().toolCalls().front().status, holonight_domain::ToolInvocationStatus::Running);

  QSignalSpy changed(viewModel.messages(), &QAbstractItemModel::dataChanged);
  executor->complete(QJsonObject{
      {QStringLiteral("entries"), QJsonArray{QJsonObject{{QStringLiteral("name"), QStringLiteral("photo.jpg")},
                                                         {QStringLiteral("type"), QStringLiteral("file")}}}}});

  ASSERT_EQ(anthropicHttpClient->streamingCallCount(), 2U);
  const QJsonDocument followUp = QJsonDocument::fromJson(anthropicHttpClient->streamingCall(1).request.body);
  ASSERT_TRUE(followUp.isObject());
  const QJsonArray followUpMessages = followUp.object().value(QStringLiteral("messages")).toArray();
  ASSERT_FALSE(followUpMessages.isEmpty());
  const QJsonObject toolResultMessage = followUpMessages.at(followUpMessages.size() - 1).toObject();
  ASSERT_EQ(toolResultMessage.value(QStringLiteral("role")).toString(), QStringLiteral("user"));
  ASSERT_TRUE(toolResultMessage.value(QStringLiteral("content")).isArray());
  EXPECT_EQ(toolResultMessage.value(QStringLiteral("content"))
                .toArray()
                .at(0)
                .toObject()
                .value(QStringLiteral("type"))
                .toString(),
            QStringLiteral("tool_result"));
  ASSERT_EQ(viewModel.messages()->rowCount(), 4);
  const QVariantMap completed = toolCallAt(*viewModel.messages(), 1);
  EXPECT_EQ(completed.value(QStringLiteral("status")).toString(), QStringLiteral("completed"));
  EXPECT_EQ(completed.value(QStringLiteral("rendererKey")).toString(), QStringLiteral("filesystem.list"));
  EXPECT_EQ(completed.value(QStringLiteral("toolTitle")).toString(), QStringLiteral("Listed ~/Pictures"));
  EXPECT_EQ(completed.value(QStringLiteral("detailData")).toMap().value(QStringLiteral("fileCount")).toInt(), 1);
  EXPECT_TRUE(completed.value(QStringLiteral("rawResultAvailable")).toBool());
  EXPECT_FALSE(completed.value(QStringLiteral("isError")).toBool());
  EXPECT_GE(changed.count(), 1);

  int visibleToolActivities = 0;
  for (int row = 0; row < viewModel.messages()->rowCount(); ++row) {
    if (!toolCallAt(*viewModel.messages(), row).isEmpty()) {
      ++visibleToolActivities;
    }
  }
  EXPECT_EQ(visibleToolActivities, 1);

  const auto persistedCompleted = repository->allMessages(viewModel.activeConversationId());
  ASSERT_EQ(persistedCompleted.size(), 5U);
  ASSERT_EQ(persistedCompleted[3].toolCalls().size(), 1U);
  EXPECT_EQ(persistedCompleted[3].toolCalls().front().status, holonight_domain::ToolInvocationStatus::Completed);
}

// Desktop-notifications cycle (REQ-F-001, REQ-F-010): responseReady is a dedicated, one-shot
// signal distinct from isStreamingChanged — both must fire on the same Completed event.
TEST(ChatViewModel, CompletedStreamEmitsResponseReadyWithConversationTitle) {
  PopulatedFixture fixture;
  QSignalSpy responseReadySpy(&fixture.view_model, &ChatViewModel::responseReady);
  QSignalSpy isStreamingSpy(&fixture.view_model, &ChatViewModel::isStreamingChanged);
  fixture.view_model.send(QStringLiteral("hello"));

  fixture.http_client->emitData(
      0, QByteArray(R"({"model":"llama3","message":{"role":"assistant","content":""},"done":true})") + "\n");

  ASSERT_EQ(responseReadySpy.count(), 1);
  EXPECT_EQ(responseReadySpy.at(0).at(0).toString(), fixture.view_model.conversation()->title());
  // isStreamingChanged still fires as before (false->true on send(), true->false on completion) —
  // the new signal is additive, not a replacement.
  EXPECT_EQ(isStreamingSpy.count(), 2);
}

// T-017: Stream error handling, stop, and regenerate (REQ-F-010, REQ-F-011, REQ-F-013,
// REQ-F-014, REQ-F-015, REQ-F-016, REQ-F-020).
TEST(ChatViewModel, StreamErrorSetsErrorMessageAndMarksMessageError) {
  PopulatedFixture fixture;
  fixture.view_model.send(QStringLiteral("hello"));

  fixture.http_client->emitError(0, QStringLiteral("boom"));

  EXPECT_EQ(fixture.view_model.errorMessage(), QStringLiteral("Ollama / llama3: boom"));
  EXPECT_EQ(statusAt(*fixture.view_model.messages(), 0), QStringLiteral("error"));
  EXPECT_TRUE(fixture.view_model.canRegenerate());
  EXPECT_FALSE(fixture.view_model.isStreaming());
}

// Desktop-notifications cycle (REQ-F-004, REQ-F-006, REQ-F-010): requestFailed carries the raw
// contextualized error, and is a dedicated signal distinct from errorMessageChanged.
TEST(ChatViewModel, StreamErrorEmitsRequestFailedWithTitleAndMessageVerbatim) {
  PopulatedFixture fixture;
  QSignalSpy requestFailedSpy(&fixture.view_model, &ChatViewModel::requestFailed);
  QSignalSpy errorMessageSpy(&fixture.view_model, &ChatViewModel::errorMessageChanged);
  fixture.view_model.send(QStringLiteral("hello"));

  fixture.http_client->emitError(0, QStringLiteral("boom"));

  ASSERT_EQ(requestFailedSpy.count(), 1);
  EXPECT_EQ(requestFailedSpy.at(0).at(0).toString(), fixture.view_model.conversation()->title());
  EXPECT_EQ(requestFailedSpy.at(0).at(1).toString(), QStringLiteral("Ollama / llama3: boom"));
  // errorMessageChanged still fires as before — the new signal is additive, not a replacement.
  EXPECT_EQ(errorMessageSpy.count(), 1);
}

TEST(ChatViewModel, StopMidStreamCancelsAndIsIdempotent) {
  PopulatedFixture fixture;
  fixture.view_model.send(QStringLiteral("hello"));
  fixture.http_client->emitData(
      0, QByteArray(R"({"model":"llama3","message":{"role":"assistant","content":"partial"},"done":false})") + "\n");

  fixture.view_model.stop();

  EXPECT_EQ(statusAt(*fixture.view_model.messages(), 0), QStringLiteral("cancelled"));
  EXPECT_EQ(textAt(*fixture.view_model.messages(), 0), QStringLiteral("partial"));
  EXPECT_FALSE(fixture.view_model.isStreaming());

  // Idempotent: nothing in flight, calling again must not crash or change state further.
  fixture.view_model.stop();
  EXPECT_EQ(statusAt(*fixture.view_model.messages(), 0), QStringLiteral("cancelled"));
}

// Desktop-notifications cycle (REQ-F-010): Cancelled must not be mistaken for Completed/Error —
// notifications are structurally unreachable for a stream the user stopped themselves.
TEST(ChatViewModel, CancelledStreamEmitsNeitherResponseReadyNorRequestFailed) {
  PopulatedFixture fixture;
  QSignalSpy responseReadySpy(&fixture.view_model, &ChatViewModel::responseReady);
  QSignalSpy requestFailedSpy(&fixture.view_model, &ChatViewModel::requestFailed);
  fixture.view_model.send(QStringLiteral("hello"));
  fixture.http_client->emitData(
      0, QByteArray(R"({"model":"llama3","message":{"role":"assistant","content":"partial"},"done":false})") + "\n");

  fixture.view_model.stop();

  EXPECT_EQ(responseReadySpy.count(), 0);
  EXPECT_EQ(requestFailedSpy.count(), 0);
}

TEST(ChatViewModel, CompletedResponseCannotBeRetried) {
  PopulatedFixture fixture;
  fixture.view_model.send(QStringLiteral("hello"));
  fixture.http_client->emitData(
      0, QByteArray(R"({"model":"llama3","message":{"role":"assistant","content":"Hi"},"done":false})") + "\n");
  fixture.http_client->emitData(
      0, QByteArray(R"({"model":"llama3","message":{"role":"assistant","content":""},"done":true})") + "\n");
  EXPECT_FALSE(fixture.view_model.canRegenerate());

  fixture.view_model.regenerate();

  EXPECT_EQ(fixture.view_model.messages()->rowCount(), 2);
  EXPECT_FALSE(fixture.view_model.isStreaming());
  EXPECT_EQ(fixture.http_client->streamingCallCount(), 1U);
}

TEST(ChatViewModel, SuccessfulRetryClearsRetryEligibility) {
  PopulatedFixture fixture;
  fixture.view_model.send(QStringLiteral("hello"));
  fixture.http_client->emitError(0, QStringLiteral("missing credentials"));
  ASSERT_TRUE(fixture.view_model.canRegenerate());

  fixture.view_model.regenerate();
  ASSERT_TRUE(fixture.view_model.isStreaming());
  EXPECT_FALSE(fixture.view_model.canRegenerate());
  ASSERT_EQ(fixture.http_client->streamingCallCount(), 2U);

  fixture.http_client->emitData(
      1, QByteArray(R"({"model":"llama3","message":{"role":"assistant","content":"Hi"},"done":false})") + "\n");
  fixture.http_client->emitData(
      1, QByteArray(R"({"model":"llama3","message":{"role":"assistant","content":""},"done":true})") + "\n");

  EXPECT_EQ(statusAt(*fixture.view_model.messages(), 0), QStringLiteral("complete"));
  EXPECT_FALSE(fixture.view_model.isStreaming());
  EXPECT_FALSE(fixture.view_model.canRegenerate());
}

TEST(ChatViewModel, RegenerateIsNoOpWhenNoEligibleAssistantMessage) {
  PopulatedFixture fixture;

  fixture.view_model.regenerate();

  EXPECT_EQ(fixture.view_model.messages()->rowCount(), 0);
  EXPECT_FALSE(fixture.view_model.isStreaming());
  EXPECT_EQ(fixture.http_client->streamingCallCount(), 0U);
}

// T-018: Shutdown safety (REQ-F-024, REQ-C-001). No ASan build variant exists in this project
// yet, so this exercises the stop()-before-destruction code path deterministically rather than
// relying on sanitizer instrumentation — a dangling-pointer regression here would still very
// likely crash the test process under a plain debug build.
TEST(ChatViewModel, DestroyingMidStreamAfterStopDoesNotCrash) {
  auto httpClient = std::make_shared<FakeHttpClient>();
  auto provider = makeProviderWithModel(httpClient);
  {
    ChatViewModel viewModel{provider, makeEmptyOpenAiProvider(), makeEmptyAnthropicProvider(),
                            makeEmptyGoogleProvider(), std::make_unique<FakeConversationRepository>()};
    viewModel.send(QStringLiteral("hello"));
    ASSERT_TRUE(viewModel.isStreaming());
    viewModel.stop();
    EXPECT_FALSE(viewModel.isStreaming());
  }
  SUCCEED();
}

TEST(ChatViewModel, DestroyingMidStreamWithoutExplicitStopIsSafe) {
  auto httpClient = std::make_shared<FakeHttpClient>();
  auto provider = makeProviderWithModel(httpClient);
  {
    ChatViewModel viewModel{provider, makeEmptyOpenAiProvider(), makeEmptyAnthropicProvider(),
                            makeEmptyGoogleProvider(), std::make_unique<FakeConversationRepository>()};
    viewModel.send(QStringLiteral("hello"));
    ASSERT_TRUE(viewModel.isStreaming());
    // No explicit stop() — the destructor must call it before conversation_ is released.
  }
  SUCCEED();
}

// T-044: Startup and persistence-unavailable fallback (REQ-F-001, REQ-F-025).
TEST(ChatViewModel, StartupWithEmptyRepositoryCreatesOnlyTransientDraft) {
  PopulatedFixture fixture;

  EXPECT_FALSE(fixture.view_model.activeConversationId().isEmpty());
  EXPECT_TRUE(fixture.repository->allConversations().isEmpty());
  EXPECT_EQ(fixture.view_model.conversationList()->rowCount(), 0);
  EXPECT_TRUE(fixture.view_model.persistenceStatusMessage().isEmpty());
}

TEST(ChatViewModel, StartupWithExistingConversationsAutoSwitchesToFront) {
  auto httpClient = std::make_shared<FakeHttpClient>();
  auto provider = makeProviderWithModel(httpClient);
  auto repositoryOwner = std::make_unique<FakeConversationRepository>();
  repositoryOwner->createConversation(QStringLiteral("Existing"));
  const QString existingId = repositoryOwner->allConversations().front().id;

  ChatViewModel viewModel{provider, makeEmptyOpenAiProvider(), makeEmptyAnthropicProvider(), makeEmptyGoogleProvider(),
                          std::move(repositoryOwner)};

  EXPECT_EQ(viewModel.activeConversationId(), existingId);
  EXPECT_EQ(viewModel.conversation()->title(), QStringLiteral("Existing"));
}

TEST(ChatViewModel, ModelRefreshPreservesRestoredConversationModel) {
  auto httpClient = std::make_shared<FakeHttpClient>();
  httpClient->enqueueBufferedSuccess(QByteArray(R"({"models":[{"name":"first"},{"name":"saved"}]})"));
  httpClient->setBufferedResponsesDeferred(true);
  auto provider = std::make_shared<OllamaProvider>(httpClient);
  auto repositoryOwner = std::make_unique<FakeConversationRepository>();
  repositoryOwner->createConversation(QStringLiteral("Existing"));
  const QString existingId = repositoryOwner->allConversations().front().id;
  repositoryOwner->updateLastModelId(existingId, holonight_domain::ModelId{.provider_id = QStringLiteral("ollama"),
                                                                           .model_name = QStringLiteral("saved")});

  ChatViewModel viewModel{provider, makeEmptyOpenAiProvider(), makeEmptyAnthropicProvider(), makeEmptyGoogleProvider(),
                          std::move(repositoryOwner)};
  EXPECT_EQ(viewModel.selectedModel().model_name, QStringLiteral("saved"));

  httpClient->completeNextBufferedCall();

  EXPECT_EQ(viewModel.selectedModel().model_name, QStringLiteral("saved"));
}

TEST(ChatViewModel, InitializationFailureSetsPersistenceStatusAndStillConstructsConversation) {
  auto httpClient = std::make_shared<FakeHttpClient>();
  auto provider = makeProviderWithModel(httpClient);
  auto repositoryOwner = std::make_unique<FakeConversationRepository>();
  repositoryOwner->simulateInitializationFailure(QStringLiteral("disk full"));

  ChatViewModel viewModel{provider, makeEmptyOpenAiProvider(), makeEmptyAnthropicProvider(), makeEmptyGoogleProvider(),
                          std::move(repositoryOwner)};

  EXPECT_EQ(viewModel.persistenceStatusMessage(), QStringLiteral("disk full"));
  ASSERT_NE(viewModel.conversation(), nullptr);
  EXPECT_TRUE(viewModel.canSend());
}

TEST(ChatViewModel, DismissPersistenceBannerClearsMessage) {
  auto httpClient = std::make_shared<FakeHttpClient>();
  auto provider = makeProviderWithModel(httpClient);
  auto repositoryOwner = std::make_unique<FakeConversationRepository>();
  repositoryOwner->simulateInitializationFailure(QStringLiteral("disk full"));
  ChatViewModel viewModel{provider, makeEmptyOpenAiProvider(), makeEmptyAnthropicProvider(), makeEmptyGoogleProvider(),
                          std::move(repositoryOwner)};
  ASSERT_FALSE(viewModel.persistenceStatusMessage().isEmpty());

  viewModel.dismissPersistenceBanner();

  EXPECT_TRUE(viewModel.persistenceStatusMessage().isEmpty());
}

// T-045: Persistence write-path and CRUD invokables (REQ-F-006, REQ-F-012, REQ-F-013, REQ-F-016,
// REQ-F-017, REQ-F-018, REQ-F-019, REQ-F-020, REQ-F-021, REQ-F-027).
TEST(ChatViewModel, FirstSendMaterializesConversationWithInitialMessagesAndModel) {
  PopulatedFixture fixture;

  fixture.view_model.send(QStringLiteral("hello"));

  EXPECT_EQ(fixture.repository->materializeConversationCallCount(), 1);
  EXPECT_EQ(fixture.repository->persistNewMessageCallCount(), 0);
  EXPECT_EQ(fixture.repository->updateLastModelIdCallCount(), 0);
  EXPECT_EQ(fixture.repository->persistMessageSettledCallCount(), 0);
  const auto persisted = fixture.repository->allMessages(fixture.view_model.activeConversationId());
  ASSERT_EQ(persisted.size(), 2U);
  EXPECT_EQ(persisted.front().status(), holonight_domain::MessageStatus::Complete);
  ASSERT_EQ(fixture.repository->allConversations().size(), 1);
  EXPECT_EQ(fixture.repository->allConversations().front().last_model_id, fixture.view_model.selectedModel());
}

TEST(ChatViewModel, SubsequentSendUsesOrdinaryMessagePersistence) {
  PopulatedFixture fixture;
  fixture.view_model.send(QStringLiteral("first"));
  fixture.http_client->emitData(
      0, QByteArray(R"({"model":"llama3","message":{"role":"assistant","content":""},"done":true})") + "\n");

  fixture.view_model.send(QStringLiteral("second"));

  EXPECT_EQ(fixture.repository->materializeConversationCallCount(), 1);
  EXPECT_EQ(fixture.repository->persistNewMessageCallCount(), 2);
  EXPECT_EQ(fixture.repository->updateLastModelIdCallCount(), 1);
  EXPECT_EQ(fixture.repository->allMessages(fixture.view_model.activeConversationId()).size(), 4U);
}

TEST(ChatViewModel, PersistMessageSettledFiresOnlyOnTerminalStatusNotOnContentDelta) {
  PopulatedFixture fixture;
  fixture.view_model.send(QStringLiteral("hello"));

  fixture.http_client->emitData(
      0, QByteArray(R"({"model":"llama3","message":{"role":"assistant","content":"Hi"},"done":false})") + "\n");
  EXPECT_EQ(fixture.repository->persistMessageSettledCallCount(), 0);

  fixture.http_client->emitData(
      0, QByteArray(R"({"model":"llama3","message":{"role":"assistant","content":""},"done":true})") + "\n");
  EXPECT_EQ(fixture.repository->persistMessageSettledCallCount(), 1);
}

TEST(ChatViewModel, FirstSendDerivesTitleAndSecondSendDoesNotRetrigger) {
  PopulatedFixture fixture;

  fixture.view_model.send(QStringLiteral("Hello, this is my first message"));
  EXPECT_EQ(fixture.view_model.conversation()->title(), QStringLiteral("Hello, this is my first message"));

  const QString titleAfterFirstSend = fixture.repository->allConversations().front().title;
  EXPECT_EQ(titleAfterFirstSend, QStringLiteral("Hello, this is my first message"));

  fixture.http_client->emitData(
      0, QByteArray(R"({"model":"llama3","message":{"role":"assistant","content":""},"done":true})") + "\n");
  fixture.view_model.send(QStringLiteral("second message"));

  EXPECT_EQ(fixture.view_model.conversation()->title(), QStringLiteral("Hello, this is my first message"));
}

// T-026 (REQ-F-003, REQ-NF-004): onStreamEvent()'s title-generation trigger only fires while
// title_source == Fallback. UtilityTaskRunner has no injectable providers, so "anthropic" (a
// Required-policy provider left without a stored credential in FakeCredentialStore) is used as the
// configured default_utility_model: its readiness resolves to MissingCredential synchronously
// (ProviderRuntimeCoordinator::resolveCredential(), no network), and onProviderReady() logs
// "not ready for title generation" whenever requestTitleGeneration() is actually reached — a safe,
// deterministic stand-in for observing "was a generation attempt made" without real network I/O.
TEST(ChatViewModel, FirstCompletedResponseAttemptsTitleGenerationWhileFallback) {
  auto httpClient = std::make_shared<FakeHttpClient>();
  auto provider = makeProviderWithModel(httpClient);
  auto credentialStore = std::make_unique<holonight_credentials::FakeCredentialStore>();
  holonight_config::UtilityConfig utilityConfig;
  utilityConfig.default_utility_model =
      holonight_domain::ModelId{.provider_id = QStringLiteral("anthropic"), .model_name = QStringLiteral("claude")};

  ScopedMessageCapture capture;
  ChatViewModel viewModel{provider,
                          makeEmptyOpenAiProvider(),
                          makeEmptyAnthropicProvider(),
                          makeEmptyGoogleProvider(),
                          std::make_unique<FakeConversationRepository>(),
                          {},
                          nullptr,
                          std::move(credentialStore),
                          utilityConfig};
  QSignalSpy listChangedSpy(viewModel.conversationList(), &QAbstractItemModel::dataChanged);

  viewModel.send(QStringLiteral("hello"));
  httpClient->emitData(
      0, QByteArray(R"({"model":"llama3","message":{"role":"assistant","content":""},"done":true})") + "\n");

  EXPECT_EQ(capture.countMessagesContaining(QStringLiteral("not ready for title generation")), 1);
  ASSERT_GE(listChangedSpy.count(), 2);
  const auto progressRole = ConversationListModel::TitleGenerationInProgressRole;
  EXPECT_EQ(listChangedSpy.at(listChangedSpy.count() - 2).at(2).value<QList<int>>(), QList<int>{progressRole});
  EXPECT_EQ(listChangedSpy.at(listChangedSpy.count() - 1).at(2).value<QList<int>>(), QList<int>{progressRole});
  EXPECT_FALSE(viewModel.conversationList()->data(viewModel.conversationList()->index(0), progressRole).toBool());
}

TEST(ChatViewModel, RestoredConversationWithAlreadyGeneratedTitleNeverAttemptsGeneration) {
  auto httpClient = std::make_shared<FakeHttpClient>();
  auto provider = makeProviderWithModel(httpClient);
  auto credentialStore = std::make_unique<holonight_credentials::FakeCredentialStore>();
  holonight_config::UtilityConfig utilityConfig;
  utilityConfig.default_utility_model =
      holonight_domain::ModelId{.provider_id = QStringLiteral("anthropic"), .model_name = QStringLiteral("claude")};

  auto repositoryOwner = std::make_unique<FakeConversationRepository>();
  repositoryOwner->createConversation(QStringLiteral("Existing"));
  const QString existingId = repositoryOwner->allConversations().front().id;
  repositoryOwner->renameConversation(existingId, QStringLiteral("Already Generated"),
                                      holonight_domain::TitleSource::Generated);

  ScopedMessageCapture capture;
  ChatViewModel viewModel{provider,
                          makeEmptyOpenAiProvider(),
                          makeEmptyAnthropicProvider(),
                          makeEmptyGoogleProvider(),
                          std::move(repositoryOwner),
                          {},
                          nullptr,
                          std::move(credentialStore),
                          utilityConfig};
  ASSERT_EQ(viewModel.activeConversationId(), existingId);

  viewModel.send(QStringLiteral("hello"));
  httpClient->emitData(
      0, QByteArray(R"({"model":"llama3","message":{"role":"assistant","content":""},"done":true})") + "\n");

  EXPECT_EQ(capture.countMessagesContaining(QStringLiteral("not ready for title generation")), 0);
}

TEST(ChatViewModel, NewChatOnUnusedTransientDraftIsNoOp) {
  PopulatedFixture fixture;
  const QString firstId = fixture.view_model.activeConversationId();

  fixture.view_model.createConversation();

  EXPECT_EQ(fixture.view_model.activeConversationId(), firstId);
  EXPECT_TRUE(fixture.repository->allConversations().isEmpty());
  EXPECT_EQ(fixture.view_model.conversationList()->rowCount(), 0);
}

TEST(ChatViewModel, RepeatedNewChatPreservesTransientDraftInput) {
  PopulatedFixture fixture;
  const QString draftId = fixture.view_model.activeConversationId();
  fixture.view_model.setInputText(QStringLiteral("unfinished thought"));

  fixture.view_model.createConversation();
  fixture.view_model.createConversation();

  EXPECT_EQ(fixture.view_model.activeConversationId(), draftId);
  EXPECT_EQ(fixture.view_model.inputText(), QStringLiteral("unfinished thought"));
  EXPECT_TRUE(fixture.repository->allConversations().isEmpty());
}

TEST(ChatViewModel, SwitchConversationStopsActiveStreamFirst) {
  PopulatedFixture fixture;
  fixture.view_model.send(QStringLiteral("hello"));
  ASSERT_TRUE(fixture.view_model.isStreaming());

  fixture.view_model.createConversation();

  EXPECT_FALSE(fixture.view_model.isStreaming());
}

TEST(ChatViewModel, NewChatFromPersistedConversationClearsMessagesAndPreservesInput) {
  auto repositoryOwner = std::make_unique<FakeConversationRepository>();
  auto* repository = repositoryOwner.get();
  repository->createConversation(QStringLiteral("Existing"));
  ChatViewModel viewModel{makeProviderWithModel(std::make_shared<FakeHttpClient>()), makeEmptyOpenAiProvider(),
                          makeEmptyAnthropicProvider(), makeEmptyGoogleProvider(), std::move(repositoryOwner)};
  const QString persistedId = viewModel.activeConversationId();
  viewModel.setInputText(QStringLiteral("carry this draft"));

  viewModel.createConversation();

  EXPECT_NE(viewModel.activeConversationId(), persistedId);
  EXPECT_EQ(viewModel.messages()->rowCount(), 0);
  EXPECT_EQ(viewModel.inputText(), QStringLiteral("carry this draft"));
  EXPECT_EQ(repository->allConversations().size(), 1);
  EXPECT_EQ(viewModel.conversationList()->rowCount(), 1);
}

TEST(ChatViewModel, RenameConversationUpdatesActiveConversationTitleInPlace) {
  auto repositoryOwner = std::make_unique<FakeConversationRepository>();
  auto* repository = repositoryOwner.get();
  repository->createConversation(QStringLiteral("Existing"));
  ChatViewModel viewModel{makeProviderWithModel(std::make_shared<FakeHttpClient>()), makeEmptyOpenAiProvider(),
                          makeEmptyAnthropicProvider(), makeEmptyGoogleProvider(), std::move(repositoryOwner)};
  const QString activeId = viewModel.activeConversationId();

  viewModel.renameConversation(activeId, QStringLiteral("Renamed"));

  EXPECT_EQ(viewModel.conversation()->title(), QStringLiteral("Renamed"));
  EXPECT_EQ(repository->allConversations().front().title, QStringLiteral("Renamed"));
}

TEST(ChatViewModel, FailedRenamePreservesActiveConversationTitle) {
  auto repositoryOwner = std::make_unique<FakeConversationRepository>();
  auto* repository = repositoryOwner.get();
  repository->createConversation(QStringLiteral("Existing"));
  ChatViewModel viewModel{makeProviderWithModel(std::make_shared<FakeHttpClient>()), makeEmptyOpenAiProvider(),
                          makeEmptyAnthropicProvider(), makeEmptyGoogleProvider(), std::move(repositoryOwner)};
  const QString activeId = viewModel.activeConversationId();
  const QString originalTitle = viewModel.conversation()->title();
  repository->failNextRename(QStringLiteral("database busy"));

  viewModel.renameConversation(activeId, QStringLiteral("Not persisted"));

  EXPECT_EQ(viewModel.conversation()->title(), originalTitle);
  EXPECT_EQ(repository->allConversations().front().title, originalTitle);
  EXPECT_EQ(viewModel.errorMessage(), QStringLiteral("database busy"));
}

TEST(ChatViewModel, DeleteActiveConversationFallsBackToNewConversationWhenNoneRemain) {
  auto httpClient = std::make_shared<FakeHttpClient>();
  auto repositoryOwner = std::make_unique<FakeConversationRepository>();
  auto* repository = repositoryOwner.get();
  repository->createConversation(QStringLiteral("Existing"));
  const QString activeId = repository->allConversations().front().id;
  ChatViewModel viewModel{makeProviderWithModel(httpClient), makeEmptyOpenAiProvider(), makeEmptyAnthropicProvider(),
                          makeEmptyGoogleProvider(), std::move(repositoryOwner)};

  viewModel.deleteConversation(activeId);

  EXPECT_NE(viewModel.activeConversationId(), activeId);
  EXPECT_FALSE(viewModel.activeConversationId().isEmpty());
  EXPECT_TRUE(repository->allConversations().isEmpty());
  EXPECT_EQ(viewModel.conversationList()->rowCount(), 0);
}

TEST(ChatViewModel, DeleteActiveConversationSwitchesToRemainingConversation) {
  auto repositoryOwner = std::make_unique<FakeConversationRepository>();
  auto* repository = repositoryOwner.get();
  repository->createConversation(QStringLiteral("First"));
  const QString firstId = repository->allConversations().front().id;
  repository->createConversation(QStringLiteral("Second"));
  const auto summaries = repository->allConversations();
  const QString secondId = summaries.front().id == firstId ? summaries.back().id : summaries.front().id;
  ChatViewModel viewModel{makeProviderWithModel(std::make_shared<FakeHttpClient>()), makeEmptyOpenAiProvider(),
                          makeEmptyAnthropicProvider(), makeEmptyGoogleProvider(), std::move(repositoryOwner)};
  if (viewModel.activeConversationId() != secondId) {
    viewModel.switchConversation(secondId);
  }
  ASSERT_NE(firstId, secondId);

  viewModel.deleteConversation(secondId);

  EXPECT_EQ(viewModel.activeConversationId(), firstId);
  EXPECT_EQ(repository->allConversations().size(), 1);
}

TEST(ChatViewModel, PinConversationUpdatesConversationListInPlaceViaSignal) {
  auto repositoryOwner = std::make_unique<FakeConversationRepository>();
  auto* repository = repositoryOwner.get();
  repository->createConversation(QStringLiteral("First"));
  const QString firstId = repository->allConversations().front().id;
  repository->createConversation(QStringLiteral("Second"));
  const auto summaries = repository->allConversations();
  const QString secondId = summaries.front().id == firstId ? summaries.back().id : summaries.front().id;
  ChatViewModel viewModel{makeProviderWithModel(std::make_shared<FakeHttpClient>()), makeEmptyOpenAiProvider(),
                          makeEmptyAnthropicProvider(), makeEmptyGoogleProvider(), std::move(repositoryOwner)};
  const int listCallsBeforePin = repository->listConversationsCallCount();

  viewModel.pinConversation(secondId);

  EXPECT_EQ(repository->listConversationsCallCount(), listCallsBeforePin);
  ASSERT_EQ(viewModel.conversationList()->rowCount(), 2);
  EXPECT_EQ(conversationIdAt(*viewModel.conversationList(), 0), secondId);
  EXPECT_TRUE(conversationPinnedAt(*viewModel.conversationList(), 0));
}

TEST(ChatViewModel, PinConversationEmitsConversationPinnedWithMatchingUuid) {
  auto repositoryOwner = std::make_unique<FakeConversationRepository>();
  repositoryOwner->createConversation(QStringLiteral("Existing"));
  ChatViewModel viewModel{makeProviderWithModel(std::make_shared<FakeHttpClient>()), makeEmptyOpenAiProvider(),
                          makeEmptyAnthropicProvider(), makeEmptyGoogleProvider(), std::move(repositoryOwner)};
  const QString activeId = viewModel.activeConversationId();
  QSignalSpy pinnedSpy(&viewModel, &ChatViewModel::conversationPinned);

  viewModel.pinConversation(activeId);

  ASSERT_EQ(pinnedSpy.count(), 1);
  EXPECT_EQ(pinnedSpy.front().front().value<QUuid>(), QUuid::fromString(activeId));
}

TEST(ChatViewModel, UnpinConversationReinsertsAmongUnpinnedConversations) {
  auto repositoryOwner = std::make_unique<FakeConversationRepository>();
  auto* repository = repositoryOwner.get();
  repository->createConversation(QStringLiteral("First"));
  const QString firstId = repository->allConversations().front().id;
  repository->createConversation(QStringLiteral("Second"));
  ChatViewModel viewModel{makeProviderWithModel(std::make_shared<FakeHttpClient>()), makeEmptyOpenAiProvider(),
                          makeEmptyAnthropicProvider(), makeEmptyGoogleProvider(), std::move(repositoryOwner)};
  viewModel.pinConversation(firstId);
  ASSERT_EQ(conversationIdAt(*viewModel.conversationList(), 0), firstId);
  ASSERT_TRUE(conversationPinnedAt(*viewModel.conversationList(), 0));

  viewModel.unpinConversation(firstId);

  ASSERT_EQ(viewModel.conversationList()->rowCount(), 2);
  EXPECT_FALSE(conversationPinnedAt(*viewModel.conversationList(), 0));
  EXPECT_FALSE(conversationPinnedAt(*viewModel.conversationList(), 1));
  EXPECT_FALSE(repository->allConversations().front().pinned_at.has_value());
}

TEST(ChatViewModel, UnpinConversationEmitsConversationUnpinnedWithMatchingUuid) {
  auto repositoryOwner = std::make_unique<FakeConversationRepository>();
  repositoryOwner->createConversation(QStringLiteral("Existing"));
  ChatViewModel viewModel{makeProviderWithModel(std::make_shared<FakeHttpClient>()), makeEmptyOpenAiProvider(),
                          makeEmptyAnthropicProvider(), makeEmptyGoogleProvider(), std::move(repositoryOwner)};
  const QString activeId = viewModel.activeConversationId();
  viewModel.pinConversation(activeId);
  QSignalSpy unpinnedSpy(&viewModel, &ChatViewModel::conversationUnpinned);

  viewModel.unpinConversation(activeId);

  ASSERT_EQ(unpinnedSpy.count(), 1);
  EXPECT_EQ(unpinnedSpy.front().front().value<QUuid>(), QUuid::fromString(activeId));
}

TEST(ChatViewModel, RenamePinnedConversationStaysInPinnedSectionAtSamePosition) {
  auto repositoryOwner = std::make_unique<FakeConversationRepository>();
  auto* repository = repositoryOwner.get();
  repository->createConversation(QStringLiteral("First"));
  const QString firstId = repository->allConversations().front().id;
  repository->createConversation(QStringLiteral("Second"));
  ChatViewModel viewModel{makeProviderWithModel(std::make_shared<FakeHttpClient>()), makeEmptyOpenAiProvider(),
                          makeEmptyAnthropicProvider(), makeEmptyGoogleProvider(), std::move(repositoryOwner)};
  viewModel.pinConversation(firstId);
  ASSERT_EQ(conversationIdAt(*viewModel.conversationList(), 0), firstId);

  viewModel.renameConversation(firstId, QStringLiteral("Renamed while pinned"));

  EXPECT_EQ(conversationIdAt(*viewModel.conversationList(), 0), firstId);
  EXPECT_TRUE(conversationPinnedAt(*viewModel.conversationList(), 0));
  EXPECT_EQ(viewModel.conversationList()
                ->data(viewModel.conversationList()->index(0), ConversationListModel::TitleRole)
                .toString(),
            QStringLiteral("Renamed while pinned"));
}

TEST(ChatViewModel, NonFatalRepositoryErrorReusesTransientErrorMessage) {
  PopulatedFixture fixture;

  emit fixture.repository->error(QStringLiteral("some-id"), QStringLiteral("constraint violation"));

  EXPECT_EQ(fixture.view_model.errorMessage(), QStringLiteral("constraint violation"));
  EXPECT_TRUE(fixture.view_model.persistenceStatusMessage().isEmpty());
}

TEST(ChatViewModel, FatalMidSessionUnavailableDisablesPersistenceForFutureSends) {
  PopulatedFixture fixture;
  ASSERT_EQ(fixture.repository->persistNewMessageCallCount(), 0);

  emit fixture.repository->unavailable(QStringLiteral("connection lost"));
  EXPECT_EQ(fixture.view_model.persistenceStatusMessage(), QStringLiteral("connection lost"));

  fixture.view_model.send(QStringLiteral("hello"));

  EXPECT_EQ(fixture.repository->persistNewMessageCallCount(), 0);
}

TEST(ChatViewModel, AvailableProvidersAreStableAndModelsAreFiltered) {
  auto ollamaHttpClient = std::make_shared<FakeHttpClient>();
  auto ollamaProvider = makeProviderWithModel(ollamaHttpClient);
  auto openAiHttpClient = std::make_shared<FakeHttpClient>();
  auto openAiProvider = makeOpenAiProviderWithModel(openAiHttpClient);

  ChatViewModel viewModel{ollamaProvider, openAiProvider, makeEmptyAnthropicProvider(), makeEmptyGoogleProvider(),
                          std::make_unique<FakeConversationRepository>()};

  const QVariantList providers = viewModel.availableProviders();
  ASSERT_EQ(providers.size(), 2);
  EXPECT_EQ(providers.at(0).toMap().value(QStringLiteral("provider_id")).toString(), QStringLiteral("ollama"));
  EXPECT_EQ(providers.at(1).toMap().value(QStringLiteral("provider_id")).toString(), QStringLiteral("openai"));
  EXPECT_EQ(viewModel.availableModelNames(), QStringList{QStringLiteral("gpt-4o")});

  viewModel.setSelectedProviderId(QStringLiteral("ollama"));

  EXPECT_EQ(viewModel.availableModelNames(), QStringList{QStringLiteral("llama3")});
  EXPECT_EQ(viewModel.selectedModelName(), QStringLiteral("llama3"));
}

TEST(ChatViewModel, SelectingOpenAiModelRoutesSendToOpenAiProviderOnly) {
  auto ollamaHttpClient = std::make_shared<FakeHttpClient>();
  auto ollamaProvider = makeProviderWithModel(ollamaHttpClient);
  auto openAiHttpClient = std::make_shared<FakeHttpClient>();
  auto openAiProvider = makeOpenAiProviderWithModel(openAiHttpClient);

  ChatViewModel viewModel{ollamaProvider, openAiProvider, makeEmptyAnthropicProvider(), makeEmptyGoogleProvider(),
                          std::make_unique<FakeConversationRepository>()};

  viewModel.setSelectedProviderId(QStringLiteral("openai"));

  viewModel.send(QStringLiteral("hello"));

  EXPECT_EQ(openAiHttpClient->streamingCallCount(), 1U);
  EXPECT_EQ(ollamaHttpClient->streamingCallCount(), 0U);
}

TEST(ChatViewModel, ModelRefreshRestoresPreviouslySelectedOpenAiModel) {
  auto ollamaHttpClient = std::make_shared<FakeHttpClient>();
  ollamaHttpClient->enqueueBufferedSuccess(QByteArray(R"({"models":[{"name":"llama3"}]})"));
  auto ollamaProvider = std::make_shared<OllamaProvider>(ollamaHttpClient);

  auto openAiHttpClient = std::make_shared<FakeHttpClient>();
  openAiHttpClient->enqueueBufferedSuccess(QByteArray(R"({"data":[{"id":"gpt-4o"},{"id":"gpt-4o-mini"}]})"));
  openAiHttpClient->setBufferedResponsesDeferred(true);
  auto openAiProvider = std::make_shared<OpenAIProvider>(openAiHttpClient);

  auto repositoryOwner = std::make_unique<FakeConversationRepository>();
  repositoryOwner->createConversation(QStringLiteral("Existing"));
  const QString existingId = repositoryOwner->allConversations().front().id;
  repositoryOwner->updateLastModelId(
      existingId,
      holonight_domain::ModelId{.provider_id = QStringLiteral("openai"), .model_name = QStringLiteral("gpt-4o-mini")});

  ChatViewModel viewModel{ollamaProvider, openAiProvider, makeEmptyAnthropicProvider(), makeEmptyGoogleProvider(),
                          std::move(repositoryOwner)};
  EXPECT_EQ(viewModel.selectedModel().provider_id, QStringLiteral("openai"));
  EXPECT_EQ(viewModel.selectedModel().model_name, QStringLiteral("gpt-4o-mini"));

  openAiProvider->refresh([&viewModel] { viewModel.syncAvailableOpenAiModels(); }, [](const QString& /*reason*/) {});
  openAiHttpClient->completeNextBufferedCall();

  EXPECT_EQ(viewModel.selectedModel().provider_id, QStringLiteral("openai"));
  EXPECT_EQ(viewModel.selectedModel().model_name, QStringLiteral("gpt-4o-mini"));
  EXPECT_TRUE(viewModel.canSend());
}

// Regression test for a real-world bug: on restart, a conversation last used with an OpenAI model
// reloaded with an unrelated Ollama model shown in the dropdown. Root cause was a race in
// syncAvailableModels(): if Ollama's (local, fast) refresh completes *after* adoptConversation()
// has already restored the persisted OpenAI selection, but *before* OpenAI's (network) refresh has
// completed, the combined model list at that moment contains only Ollama's models — and the old
// code treated "selection not in the current list" as proof the selection was stale, clobbering it
// with models.front(). Unlike ModelRefreshRestoresPreviouslySelectedOpenAiModel above (which only
// defers the *same* provider the selection belongs to, and can therefore never reproduce this), this
// test defers the *other* provider to reproduce the actual race.
TEST(ChatViewModel, LateOllamaRefreshDoesNotClobberPersistedOpenAiSelectionWhileOpenAiStillPending) {
  auto ollamaHttpClient = std::make_shared<FakeHttpClient>();
  ollamaHttpClient->enqueueBufferedSuccess(QByteArray(R"({"models":[{"name":"llama3"}]})"));
  ollamaHttpClient->setBufferedResponsesDeferred(true);
  auto ollamaProvider = std::make_shared<OllamaProvider>(ollamaHttpClient);

  auto openAiHttpClient = std::make_shared<FakeHttpClient>();
  openAiHttpClient->enqueueBufferedSuccess(QByteArray(R"({"data":[{"id":"gpt-5.6-luna"}]})"));
  openAiHttpClient->setBufferedResponsesDeferred(true);
  auto openAiProvider = std::make_shared<OpenAIProvider>(openAiHttpClient);

  auto repositoryOwner = std::make_unique<FakeConversationRepository>();
  repositoryOwner->createConversation(QStringLiteral("Existing"));
  const QString existingId = repositoryOwner->allConversations().front().id;
  repositoryOwner->updateLastModelId(
      existingId,
      holonight_domain::ModelId{.provider_id = QStringLiteral("openai"), .model_name = QStringLiteral("gpt-5.6-luna")});

  ChatViewModel viewModel{ollamaProvider, openAiProvider, makeEmptyAnthropicProvider(), makeEmptyGoogleProvider(),
                          std::move(repositoryOwner)};
  ASSERT_EQ(viewModel.selectedModel().provider_id, QStringLiteral("openai"));
  ASSERT_EQ(viewModel.selectedModel().model_name, QStringLiteral("gpt-5.6-luna"));

  openAiProvider->refresh([&viewModel] { viewModel.syncAvailableOpenAiModels(); }, [](const QString& /*reason*/) {});

  // Ollama's refresh finally completes here — after the persisted conversation already restored
  // the OpenAI selection, but while OpenAI's own refresh is still pending.
  ollamaHttpClient->completeNextBufferedCall();

  EXPECT_EQ(viewModel.selectedModel().provider_id, QStringLiteral("openai"));
  EXPECT_EQ(viewModel.selectedModel().model_name, QStringLiteral("gpt-5.6-luna"));
}

TEST(ChatViewModel, ConstructionDoesNotRefreshOpenAiBeforeCredentialsAreAvailable) {
  auto ollamaHttpClient = std::make_shared<FakeHttpClient>();
  ollamaHttpClient->enqueueBufferedSuccess(QByteArray(R"({"models":[{"name":"llama3"}]})"));
  ollamaHttpClient->setBufferedResponsesDeferred(true);
  auto ollamaProvider = std::make_shared<OllamaProvider>(ollamaHttpClient);

  auto openAiHttpClient = std::make_shared<FakeHttpClient>();
  auto openAiProvider = std::make_shared<OpenAIProvider>(openAiHttpClient);

  auto repositoryOwner = std::make_unique<FakeConversationRepository>();
  repositoryOwner->createConversation(QStringLiteral("Existing"));
  const QString existingId = repositoryOwner->allConversations().front().id;
  repositoryOwner->updateLastModelId(
      existingId,
      holonight_domain::ModelId{.provider_id = QStringLiteral("openai"), .model_name = QStringLiteral("gpt-5.6-luna")});

  ChatViewModel viewModel{ollamaProvider, openAiProvider, makeEmptyAnthropicProvider(), makeEmptyGoogleProvider(),
                          std::move(repositoryOwner)};
  EXPECT_EQ(openAiHttpClient->bufferedCallCount(), 0U);
  ASSERT_EQ(viewModel.selectedModel().provider_id, QStringLiteral("openai"));
  ASSERT_EQ(viewModel.selectedModel().model_name, QStringLiteral("gpt-5.6-luna"));

  ollamaHttpClient->completeNextBufferedCall();

  EXPECT_EQ(viewModel.selectedModel().provider_id, QStringLiteral("openai"));
  EXPECT_EQ(viewModel.selectedModel().model_name, QStringLiteral("gpt-5.6-luna"));
}

TEST(ChatViewModel, InstanceIdsDriveProjectionDefaultsMemoryAndSendRouting) {
  holonight_config::OllamaProviderConfig firstSettings;
  firstSettings.default_model = QStringLiteral("first-default");
  holonight_config::OllamaProviderConfig secondSettings;
  secondSettings.default_model = QStringLiteral("second-default");
  holonight_config::ProviderState state{.instances = {{.id = QStringLiteral("ollama-home"),
                                                       .type = holonight_config::ProviderType::Ollama,
                                                       .display_name = QStringLiteral("Home Ollama"),
                                                       .enabled = true,
                                                       .settings = firstSettings},
                                                      {.id = QStringLiteral("ollama-lab"),
                                                       .type = holonight_config::ProviderType::Ollama,
                                                       .display_name = QStringLiteral("Lab Ollama"),
                                                       .enabled = true,
                                                       .settings = secondSettings}}};
  auto firstClient = std::make_shared<FakeHttpClient>();
  auto secondClient = std::make_shared<FakeHttpClient>();
  auto router = std::make_unique<ProviderAdapterRouter>();
  ASSERT_TRUE(router->add(state.instances[0], firstClient));
  ASSERT_TRUE(router->add(state.instances[1], secondClient));
  ASSERT_TRUE(router->restoreAvailableModels(
      QStringLiteral("ollama-home"),
      {{.provider_id = QStringLiteral("ollama-home"), .model_name = QStringLiteral("first-default")},
       {.provider_id = QStringLiteral("ollama-home"), .model_name = QStringLiteral("first-remembered")}}));
  ASSERT_TRUE(router->restoreAvailableModels(
      QStringLiteral("ollama-lab"),
      {{.provider_id = QStringLiteral("ollama-lab"), .model_name = QStringLiteral("second-other")},
       {.provider_id = QStringLiteral("ollama-lab"), .model_name = QStringLiteral("second-default")}}));

  ChatViewModel viewModel{makeProviderWithNoModels(std::make_shared<FakeHttpClient>()),
                          makeEmptyOpenAiProvider(),
                          makeEmptyAnthropicProvider(),
                          makeEmptyGoogleProvider(),
                          std::make_unique<FakeConversationRepository>(),
                          {},
                          nullptr,
                          nullptr,
                          {},
                          {},
                          state,
                          std::move(router)};

  const QVariantList providers = viewModel.availableProviders();
  ASSERT_EQ(providers.size(), 2);
  EXPECT_EQ(providers[0].toMap().value(QStringLiteral("provider_id")).toString(), QStringLiteral("ollama-home"));
  EXPECT_EQ(providers[0].toMap().value(QStringLiteral("display_name")).toString(), QStringLiteral("Home Ollama"));
  EXPECT_EQ(providers[1].toMap().value(QStringLiteral("provider_id")).toString(), QStringLiteral("ollama-lab"));

  viewModel.setSelectedModelName(QStringLiteral("first-remembered"));
  viewModel.setSelectedProviderId(QStringLiteral("ollama-lab"));
  EXPECT_EQ(viewModel.selectedModelName(), QStringLiteral("second-default"));
  viewModel.setSelectedProviderId(QStringLiteral("ollama-home"));
  EXPECT_EQ(viewModel.selectedModelName(), QStringLiteral("first-remembered"));

  viewModel.setSelectedProviderId(QStringLiteral("ollama-lab"));
  viewModel.send(QStringLiteral("route this"));
  EXPECT_EQ(firstClient->streamingCallCount(), 0U);
  EXPECT_EQ(secondClient->streamingCallCount(), 1U);
  ASSERT_EQ(viewModel.conversation()->messages().size(), 2U);
  EXPECT_EQ(viewModel.conversation()->messages().back().modelId()->provider_id, QStringLiteral("ollama-lab"));

  secondClient->emitError(0, QStringLiteral("boom"));
  EXPECT_EQ(viewModel.errorMessage(), QStringLiteral("Lab Ollama / second-default: boom"));
}

TEST(ChatViewModel, InstanceProjectionExcludesDisabledAndModelLessProvidersInSavedOrder) {
  holonight_config::ProviderState state{.instances = {
                                            {.id = QStringLiteral("enabled-second"),
                                             .type = holonight_config::ProviderType::Ollama,
                                             .display_name = QStringLiteral("Zulu"),
                                             .enabled = true,
                                             .settings = holonight_config::OllamaProviderConfig{}},
                                            {.id = QStringLiteral("disabled"),
                                             .type = holonight_config::ProviderType::Ollama,
                                             .display_name = QStringLiteral("Disabled"),
                                             .enabled = false,
                                             .settings = holonight_config::OllamaProviderConfig{}},
                                            {.id = QStringLiteral("model-less"),
                                             .type = holonight_config::ProviderType::Ollama,
                                             .display_name = QStringLiteral("Model-less"),
                                             .enabled = true,
                                             .settings = holonight_config::OllamaProviderConfig{}},
                                            {.id = QStringLiteral("enabled-fourth"),
                                             .type = holonight_config::ProviderType::Ollama,
                                             .display_name = QStringLiteral("Alpha"),
                                             .enabled = true,
                                             .settings = holonight_config::OllamaProviderConfig{}},
                                        }};
  auto router = std::make_unique<ProviderAdapterRouter>();
  for (const auto& instance : state.instances) {
    ASSERT_TRUE(router->add(instance, std::make_shared<FakeHttpClient>()));
  }
  ASSERT_TRUE(router->restoreAvailableModels(
      QStringLiteral("enabled-second"),
      {{.provider_id = QStringLiteral("enabled-second"), .model_name = QStringLiteral("zulu-model")}}));
  ASSERT_TRUE(router->restoreAvailableModels(
      QStringLiteral("disabled"),
      {{.provider_id = QStringLiteral("disabled"), .model_name = QStringLiteral("disabled-model")}}));
  ASSERT_TRUE(router->restoreAvailableModels(
      QStringLiteral("enabled-fourth"),
      {{.provider_id = QStringLiteral("enabled-fourth"), .model_name = QStringLiteral("alpha-model")}}));

  ChatViewModel viewModel{makeProviderWithNoModels(std::make_shared<FakeHttpClient>()),
                          makeEmptyOpenAiProvider(),
                          makeEmptyAnthropicProvider(),
                          makeEmptyGoogleProvider(),
                          std::make_unique<FakeConversationRepository>(),
                          {},
                          nullptr,
                          nullptr,
                          {},
                          {},
                          state,
                          std::move(router)};

  const QVariantList providers = viewModel.availableProviders();
  ASSERT_EQ(providers.size(), 2);
  EXPECT_EQ(providers[0].toMap().value(QStringLiteral("provider_id")).toString(), QStringLiteral("enabled-second"));
  EXPECT_EQ(providers[0].toMap().value(QStringLiteral("display_name")).toString(), QStringLiteral("Zulu"));
  EXPECT_EQ(providers[1].toMap().value(QStringLiteral("provider_id")).toString(), QStringLiteral("enabled-fourth"));
  EXPECT_EQ(providers[1].toMap().value(QStringLiteral("display_name")).toString(), QStringLiteral("Alpha"));
}

TEST(ChatViewModel, ApplyingCommittedProviderStateUpdatesChatProjectionWithoutRestart) {
  holonight_config::ProviderState state{.instances = {
                                            {.id = QStringLiteral("first-instance"),
                                             .type = holonight_config::ProviderType::Ollama,
                                             .display_name = QStringLiteral("First name"),
                                             .enabled = true,
                                             .settings = holonight_config::OllamaProviderConfig{}},
                                            {.id = QStringLiteral("second-instance"),
                                             .type = holonight_config::ProviderType::Ollama,
                                             .display_name = QStringLiteral("Second name"),
                                             .enabled = true,
                                             .settings = holonight_config::OllamaProviderConfig{}},
                                        }};
  auto router = std::make_unique<ProviderAdapterRouter>();
  for (const auto& instance : state.instances) {
    ASSERT_TRUE(router->add(instance, std::make_shared<FakeHttpClient>()));
    ASSERT_TRUE(router->restoreAvailableModels(
        instance.id, {{.provider_id = instance.id, .model_name = QStringLiteral("a-model")}}));
  }

  ChatViewModel viewModel{makeProviderWithNoModels(std::make_shared<FakeHttpClient>()),
                          makeEmptyOpenAiProvider(),
                          makeEmptyAnthropicProvider(),
                          makeEmptyGoogleProvider(),
                          std::make_unique<FakeConversationRepository>(),
                          {},
                          nullptr,
                          nullptr,
                          {},
                          {},
                          state,
                          std::move(router)};
  ASSERT_EQ(viewModel.availableProviders().size(), 2);
  viewModel.messages()->insertNewestMessage(
      holonight_domain::Message(holonight_domain::MessageId::generate(), holonight_domain::MessageRole::Assistant,
                                QStringLiteral("historical response"), holonight_domain::MessageStatus::Complete, {},
                                holonight_domain::ModelId{.provider_id = QStringLiteral("second-instance"),
                                                          .model_name = QStringLiteral("a-model")}));
  EXPECT_EQ(viewModel.messages()->data(viewModel.messages()->index(0), MessageListModel::ProviderNameRole).toString(),
            QStringLiteral("Second name"));

  state.instances[0].enabled = false;
  state.instances[1].display_name = QStringLiteral("Renamed second");
  viewModel.applyProviderState(state);

  const QVariantList providers = viewModel.availableProviders();
  ASSERT_EQ(providers.size(), 1);
  EXPECT_EQ(providers.front().toMap().value(QStringLiteral("provider_id")).toString(),
            QStringLiteral("second-instance"));
  EXPECT_EQ(providers.front().toMap().value(QStringLiteral("display_name")).toString(),
            QStringLiteral("Renamed second"));
  EXPECT_EQ(viewModel.selectedProviderId(), QStringLiteral("second-instance"));
  EXPECT_EQ(viewModel.selectedModelName(), QStringLiteral("a-model"));
  EXPECT_EQ(viewModel.messages()->data(viewModel.messages()->index(0), MessageListModel::ProviderNameRole).toString(),
            QStringLiteral("Renamed second"));
  EXPECT_EQ(viewModel.messages()->data(viewModel.messages()->index(0), MessageListModel::ProviderTypeRole).toString(),
            QStringLiteral("ollama"));
}

TEST(ChatViewModel, InvalidRestoredInstanceFallsBackWithoutRewritingHistory) {
  holonight_config::OllamaProviderConfig settings;
  settings.default_model = QStringLiteral("configured-default");
  holonight_config::ProviderState state{.instances = {{.id = QStringLiteral("available-instance"),
                                                       .type = holonight_config::ProviderType::Ollama,
                                                       .display_name = QStringLiteral("Available"),
                                                       .enabled = true,
                                                       .settings = settings}}};
  auto router = std::make_unique<ProviderAdapterRouter>();
  ASSERT_TRUE(router->add(state.instances.front(), std::make_shared<FakeHttpClient>()));
  ASSERT_TRUE(router->restoreAvailableModels(
      QStringLiteral("available-instance"),
      {{.provider_id = QStringLiteral("available-instance"), .model_name = QStringLiteral("other")},
       {.provider_id = QStringLiteral("available-instance"), .model_name = QStringLiteral("configured-default")}}));
  auto repository = std::make_unique<FakeConversationRepository>();
  repository->createConversation(QStringLiteral("Historical"));
  const QString conversationId = repository->allConversations().front().id;
  const holonight_domain::ModelId historical{.provider_id = QStringLiteral("deleted-instance"),
                                             .model_name = QStringLiteral("historical-model")};
  repository->updateLastModelId(conversationId, historical);
  const holonight_domain::Message historicalMessage(
      holonight_domain::MessageId::generate(), holonight_domain::MessageRole::Assistant,
      QStringLiteral("historical response"), holonight_domain::MessageStatus::Complete, {}, historical);
  repository->persistNewMessage(conversationId, historicalMessage);
  auto* repositoryOwner = repository.get();
  const int modelUpdatesBeforeRestore = repositoryOwner->updateLastModelIdCallCount();

  ChatViewModel viewModel{makeProviderWithNoModels(std::make_shared<FakeHttpClient>()),
                          makeEmptyOpenAiProvider(),
                          makeEmptyAnthropicProvider(),
                          makeEmptyGoogleProvider(),
                          std::move(repository),
                          {},
                          nullptr,
                          nullptr,
                          {},
                          {},
                          state,
                          std::move(router)};

  EXPECT_EQ(viewModel.selectedModel().provider_id, QStringLiteral("available-instance"));
  EXPECT_EQ(viewModel.selectedModel().model_name, QStringLiteral("configured-default"));
  EXPECT_TRUE(viewModel.canSend());
  EXPECT_EQ(repositoryOwner->updateLastModelIdCallCount(), modelUpdatesBeforeRestore);
  ASSERT_EQ(repositoryOwner->allConversations().size(), 1);
  EXPECT_EQ(repositoryOwner->allConversations().front().last_model_id, historical);
  ASSERT_EQ(repositoryOwner->allMessages(conversationId).size(), 1U);
  EXPECT_EQ(repositoryOwner->allMessages(conversationId).front().modelId(), historical);
}

TEST(ChatViewModel, InvalidRestoredInstanceClearsSelectionWhenNoProviderHasModels) {
  auto repository = std::make_unique<FakeConversationRepository>();
  repository->createConversation(QStringLiteral("Historical"));
  const QString conversationId = repository->allConversations().front().id;
  repository->updateLastModelId(conversationId,
                                holonight_domain::ModelId{.provider_id = QStringLiteral("deleted-instance"),
                                                          .model_name = QStringLiteral("historical-model")});

  ChatViewModel viewModel{makeProviderWithNoModels(std::make_shared<FakeHttpClient>()),
                          makeEmptyOpenAiProvider(),
                          makeEmptyAnthropicProvider(),
                          makeEmptyGoogleProvider(),
                          std::move(repository),
                          {},
                          nullptr,
                          nullptr,
                          {},
                          {},
                          {},
                          std::make_unique<ProviderAdapterRouter>()};

  EXPECT_TRUE(viewModel.selectedProviderId().isEmpty());
  EXPECT_TRUE(viewModel.selectedModelName().isEmpty());
  EXPECT_TRUE(viewModel.availableModelNames().isEmpty());
  EXPECT_FALSE(viewModel.canSend());
}

}  // namespace
}  // namespace holonight_application
