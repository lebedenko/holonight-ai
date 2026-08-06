#pragma once

#include "holonight_application/conversation_list_model.h"
#include "holonight_application/message_list_model.h"
#include "holonight_application/provider_adapter_router.h"
#include "holonight_application/provider_runtime_coordinator.h"
#include "holonight_application/utility_task_runner.h"
#include "holonight_providers/anthropic_provider.h"
#include "holonight_providers/google_provider.h"
#include "holonight_providers/ollama_provider.h"
#include "holonight_providers/openai_provider.h"

#include <QObject>
#include <QString>
#include <QUuid>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <cstddef>
#include <holonight_credentials/credential_store.h>
#include <holonight_domain/holonight_domain.h>
#include <holonight_persistence/conversation_repository.h>
#include <memory>
#include <optional>

class QQmlEngine;
class QJSEngine;

namespace holonight_application {

class ChatController;

struct ProviderDefaultModels {
  QString ollama;
  QString openai;
  QString anthropic;
  QString google;
};

// QML-facing bridge (REQ-C-002). Owns the active Conversation (rebuilt on every switch, see
// adoptConversation()), the ChatController, the OllamaProvider, and a ConversationRepository for
// the lifetime of the application window.
class ChatViewModel : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON

  Q_PROPERTY(QVariantList availableProviders READ availableProviders NOTIFY availableProvidersChanged)
  Q_PROPERTY(QStringList availableModelNames READ availableModelNames NOTIFY availableModelNamesChanged)
  Q_PROPERTY(
      QString selectedProviderId READ selectedProviderId WRITE setSelectedProviderId NOTIFY selectedProviderIdChanged)
  Q_PROPERTY(
      QString selectedModelName READ selectedModelName WRITE setSelectedModelName NOTIFY selectedModelNameChanged)
  Q_PROPERTY(ProviderStatus selectedProviderStatus READ selectedProviderStatus NOTIFY selectedProviderStatusChanged)
  Q_PROPERTY(bool canSend READ canSend NOTIFY canSendChanged)
  Q_PROPERTY(bool canRegenerate READ canRegenerate NOTIFY canRegenerateChanged)
  Q_PROPERTY(bool isStreaming READ isStreaming NOTIFY isStreamingChanged)
  Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
  Q_PROPERTY(QString providerStatusMessage READ providerStatusMessage NOTIFY providerStatusMessageChanged)
  Q_PROPERTY(QString inputText READ inputText WRITE setInputText NOTIFY inputTextChanged)
  Q_PROPERTY(holonight_application::MessageListModel* messages READ messages CONSTANT)
  Q_PROPERTY(bool messagesReady READ messagesReady NOTIFY messagesReadyChanged)
  Q_PROPERTY(holonight_application::ConversationListModel* conversationList READ conversationList CONSTANT)
  Q_PROPERTY(QString activeConversationId READ activeConversationId NOTIFY activeConversationIdChanged)
  Q_PROPERTY(QString persistenceStatusMessage READ persistenceStatusMessage NOTIFY persistenceStatusMessageChanged)

 public:
  enum class ProviderStatus : std::uint8_t {
    Idle,
    Checking,
    LoadingModels,
    Connected,
    SetupRequired,
    Unavailable,
    Error
  };
  Q_ENUM(ProviderStatus)

  // Custom singleton factory: ChatViewModel is not default-constructible (it needs an
  // OllamaProvider and a ConversationRepository), so Qt's auto-generated self-instantiation path
  // does not apply — this is registered instead (see docs/sdd/chat-window-qml/DESIGN.md §6.1).
  static ChatViewModel* create(QQmlEngine* qml_engine, QJSEngine* js_engine);

  // DI constructor: both the production create() factory and GTest fixtures funnel through this,
  // so tests never need a QQmlEngine, a real network stack, or a real database (see DESIGN.md §9).
  // initial_default_model_id (from Settings' persisted config) is consulted only when a freshly
  // adopted conversation has no last_model_id of its own — default {} keeps every existing call
  // site compiling unchanged.
  explicit ChatViewModel(std::shared_ptr<holonight_providers::OllamaProvider> ollama_provider,
                         std::shared_ptr<holonight_providers::OpenAIProvider> openai_provider,
                         std::shared_ptr<holonight_providers::AnthropicProvider> anthropic_provider,
                         std::shared_ptr<holonight_providers::GoogleProvider> google_provider,
                         std::unique_ptr<holonight_persistence::ConversationRepository> repository,
                         ProviderDefaultModels default_models = {}, QObject* parent = nullptr,
                         std::unique_ptr<holonight_credentials::CredentialStore> credential_store = nullptr,
                         holonight_config::UtilityConfig utility_config = {},
                         const UtilityProviderEndpoints& utility_endpoints = {},
                         holonight_config::ProviderState provider_state = {},
                         std::unique_ptr<ProviderAdapterRouter> adapter_router = nullptr,
                         // REQ-F-001/REQ-F-009: the same registry passed to `adapter_router`'s own
                         // constructor (so its toAnthropicToolsArray() and ChatController's
                         // invoke() agree on what "the tools" are) -- create() wires both from one
                         // instance. Defaults to nullptr like ChatController's own tool_registry
                         // parameter, so every existing test call site keeps compiling unchanged.
                         std::shared_ptr<ToolRegistry> tool_registry = nullptr);
  ~ChatViewModel() override;

  ChatViewModel(const ChatViewModel&) = delete;
  ChatViewModel& operator=(const ChatViewModel&) = delete;
  ChatViewModel(ChatViewModel&&) = delete;
  ChatViewModel& operator=(ChatViewModel&&) = delete;

  [[nodiscard]] QVariantList availableProviders() const;
  [[nodiscard]] QStringList availableModelNames() const;
  [[nodiscard]] QString selectedProviderId() const;
  void setSelectedProviderId(const QString& provider_id);
  [[nodiscard]] QString selectedModelName() const;
  void setSelectedModelName(const QString& model_name);
  [[nodiscard]] ProviderStatus selectedProviderStatus() const;
  [[nodiscard]] const holonight_domain::ModelId& selectedModel() const;
  [[nodiscard]] bool canSend() const;
  [[nodiscard]] bool canRegenerate() const;
  [[nodiscard]] bool isStreaming() const;
  [[nodiscard]] QString errorMessage() const;
  [[nodiscard]] QString providerStatusMessage() const;
  [[nodiscard]] QString inputText() const;
  void setInputText(QString text);
  [[nodiscard]] MessageListModel* messages() const;
  [[nodiscard]] bool messagesReady() const;
  [[nodiscard]] ConversationListModel* conversationList() const;
  [[nodiscard]] QString activeConversationId() const;
  [[nodiscard]] QString persistenceStatusMessage() const;

  // Plain C++ accessor (REQ-F-001's acceptance criterion calls this out explicitly). Not a
  // Q_PROPERTY: Conversation is not a Qt/QML-registered type and QML never needs it directly — it
  // renders through the `messages` MessageListModel instead.
  [[nodiscard]] holonight_domain::Conversation* conversation() const;

  // Plain C++ accessor sharing the live OllamaProvider with ProviderSettingsController (not a
  // Q_PROPERTY — only C++ singleton-to-singleton wiring needs the shared_ptr itself).
  [[nodiscard]] std::shared_ptr<holonight_providers::OllamaProvider> providerForSettings() const;

  // Plain C++ accessor sharing the live OpenAIProvider with OpenAIProviderSettingsController.
  [[nodiscard]] std::shared_ptr<holonight_providers::OpenAIProvider> openAiProviderForSettings() const;

  // Plain C++ accessor sharing the live AnthropicProvider with AnthropicProviderSettingsController.
  [[nodiscard]] std::shared_ptr<holonight_providers::AnthropicProvider> anthropicProviderForSettings() const;

  // Plain C++ accessor sharing the live GoogleProvider with GoogleProviderSettingsController.
  [[nodiscard]] std::shared_ptr<holonight_providers::GoogleProvider> googleProviderForSettings() const;
  [[nodiscard]] holonight_credentials::CredentialStore* credentialStoreForSettings() const;
  [[nodiscard]] ProviderRuntimeCoordinator* providerRuntimeCoordinator() const;

  // Plain C++ accessor (not a Q_PROPERTY — only C++ singleton-to-singleton wiring needs it), mirroring
  // providerForSettings()/openAiProviderForSettings() etc. Lets UtilitySettingsController push a saved
  // UtilityConfig into the live runner without ChatViewModel exposing utility_task_runner_ itself.
  [[nodiscard]] UtilityTaskRunner* utilityTaskRunnerForSettings() const;

  // Applies provider settings after Settings has durably committed them. This keeps the chat
  // projection and runtime router in sync without requiring an application restart.
  void applyProviderState(holonight_config::ProviderState provider_state);

  // Rebuilds the ordered provider projection and selected-provider model names, then validates the
  // canonical ModelId. A valid preferredModelId wins so Settings' saved default becomes active.
  void syncAvailableModels(const std::optional<holonight_domain::ModelId>& preferredModelId = std::nullopt);

  // Records a successful credential-aware OpenAI refresh before rebuilding the combined list.
  void syncAvailableOpenAiModels(const std::optional<holonight_domain::ModelId>& preferredModelId = std::nullopt);

  // Mirrors syncAvailableOpenAiModels(): marks anthropic_models_loaded_ before re-unioning.
  void syncAvailableAnthropicModels(const std::optional<holonight_domain::ModelId>& preferredModelId = std::nullopt);

  // Mirrors syncAvailableAnthropicModels(): marks google_models_loaded_ before re-unioning.
  void syncAvailableGoogleModels(const std::optional<holonight_domain::ModelId>& preferredModelId = std::nullopt);

  Q_INVOKABLE void send(const QString& text);
  Q_INVOKABLE void stop();
  Q_INVOKABLE void regenerate();
  Q_INVOKABLE void createConversation();
  Q_INVOKABLE void switchConversation(const QString& conversationId);
  Q_INVOKABLE void renameConversation(const QString& conversationId, const QString& newTitle);
  Q_INVOKABLE void deleteConversation(const QString& conversationId);
  Q_INVOKABLE void pinConversation(const QString& conversationId);
  Q_INVOKABLE void unpinConversation(const QString& conversationId);
  Q_INVOKABLE void dismissPersistenceBanner();

 Q_SIGNALS:
  void availableProvidersChanged();
  void availableModelNamesChanged();
  void selectedProviderIdChanged();
  void selectedModelNameChanged();
  void selectedProviderStatusChanged();
  void canSendChanged();
  void canRegenerateChanged();
  void isStreamingChanged();
  void errorMessageChanged();
  void providerStatusMessageChanged();
  void inputTextChanged();
  void activeConversationIdChanged();
  void persistenceStatusMessageChanged();
  void messagesReadyChanged();

  // One-shot notification-worthy events (not derived-state properties): fired exactly once per
  // Completed/Error stream event for the currently-adopted conversation. Consumed by
  // ChatApplication to drive desktop notifications; carry no D-Bus/visibility knowledge here.
  void responseReady(const QString& title);
  void requestFailed(const QString& title, const QString& message);

  // Deliberate deviation from the rename/delete precedent (neither is re-exposed as a ChatViewModel
  // signal): REQ-NF-007 explicitly requires ChatViewModel to own these, widening the public surface
  // for future consumers (desktop notification, analytics) without reaching into the repository
  // (DESIGN.md §6.3). QUuid per REQ-NF-007, converted from the repository/model layer's QString id.
  void conversationPinned(QUuid conversationId);
  void conversationUnpinned(QUuid conversationId);

 private:
  void onStreamEvent(const holonight_domain::StreamEvent& event);
  // REQ-F-007/REQ-F-011: a tool-calling turn can have ChatController append several new
  // Conversation messages (invocation, result, next placeholder) between two StreamEvent
  // notifications. Brings message_model_'s row count back in sync with conversation_->messages()
  // before the caller's own updateNewestMessage()-style handling of the current last message.
  void syncNewMessagesIntoModel();
  // Companion to syncNewMessagesIntoModel(), but for the repository: INSERTs any
  // conversation_->messages() appended since the last sync (tracked by persisted_message_count_)
  // so a later persistMessageSettled()/persistUsage() call on the newest message never targets a
  // row that was never INSERTed.
  void persistNewMessagesIntoRepository();
  void refreshComputedProperties();
  void setErrorMessage(QString message);
  void setProviderStatusMessage(QString message);
  void prepareSelectedProvider();
  void registerRuntimeProvider(const holonight_config::ProviderInstanceConfig& instance);
  void onProviderChanged(const QString& provider_id);
  void applySelection(holonight_domain::ModelId selection);
  [[nodiscard]] QStringList modelNames(const QString& provider_id) const;
  [[nodiscard]] QString defaultModelName(const QString& provider_id) const;
  [[nodiscard]] bool providerHasReported(const QString& provider_id) const;
  [[nodiscard]] ProviderStatus deriveSelectedProviderStatus() const;
  [[nodiscard]] QString selectModelNameForProvider(const QString& provider_id) const;
  [[nodiscard]] holonight_domain::ModelId validateModelSelection(
      const std::optional<holonight_domain::ModelId>& preferredModelId, const QVariantList& providers) const;
  [[nodiscard]] holonight_domain::ModelId selectFirstAvailableModel(const QVariantList& providers) const;
  [[nodiscard]] holonight_domain::ModelId selectReplacementModel(const holonight_domain::ModelId& current,
                                                                 const QVariantList& providers) const;
  [[nodiscard]] QString providerDisplayName(const QString& provider_id) const;
  void setPersistenceStatusMessage(QString message);
  void setMessagesReady(bool ready);

  void onRepositoryInitialized();
  void onRepositoryUnavailable(const QString& reason);
  void onConversationListLoaded(const QList<holonight_persistence::ConversationSummary>& conversations);
  void onConversationCreated(const holonight_persistence::ConversationSummary& summary);
  void onConversationLoaded(const holonight_persistence::LoadedConversation& loaded);
  void onConversationRenamed(const holonight_persistence::ConversationSummary& summary);
  void onConversationDeleted(const QString& conversationId);
  void onConversationPinned(const QString& conversationId, const QDateTime& pinnedAt);
  void onConversationUnpinned(const QString& conversationId);
  void onLastModelIdUpdated(const QString& conversationId, const holonight_domain::ModelId& modelId,
                            const QDateTime& updatedAt);
  void onRepositoryError(const QString& conversationId, const QString& message);
  void onTitleGenerated(const QString& conversationId, const QString& title);
  void onTitleGenerationStarted(const QString& conversationId);
  void onTitleGenerationFinished(const QString& conversationId);
  // REQ-F-001. Batch-load result for usageForConversation(); guarded against stale conversations
  // since the query can resolve after the user has already switched away.
  void onUsageForConversationLoaded(const QString& conversationId,
                                    const QMap<QString, holonight_persistence::UsageRecord>& usageByMessageId);
  // REQ-F-001. Live per-message usage row, fired by persistUsage() once the streamed response
  // settles; same staleness guard as onUsageForConversationLoaded.
  void onUsagePersisted(const QString& conversationId, const QString& messageId,
                        const holonight_persistence::UsageRecord& record);

  // Shared by createConversation/switchConversation/startup and delete's active-conversation
  // fallback (DESIGN.md §2.4): rebuilds conversation_ from a repository record and adopts it as
  // active. `messages` is empty for a freshly created conversation.
  void adoptConversation(const holonight_persistence::ConversationSummary& summary,
                         std::vector<holonight_domain::Message> messages);
  void adoptTransientDraft();

  std::shared_ptr<holonight_providers::OllamaProvider> ollama_provider_;
  std::shared_ptr<holonight_providers::OpenAIProvider> openai_provider_;
  std::shared_ptr<holonight_providers::AnthropicProvider> anthropic_provider_;
  std::shared_ptr<holonight_providers::GoogleProvider> google_provider_;
  std::unique_ptr<holonight_persistence::ConversationRepository> repository_;
  std::unique_ptr<holonight_credentials::CredentialStore> credential_store_;
  std::unique_ptr<ProviderRuntimeCoordinator> provider_runtime_coordinator_;
  std::unique_ptr<ProviderAdapterRouter> adapter_router_;
  std::shared_ptr<ToolRegistry> tool_registry_;
  std::unique_ptr<ChatController> chat_controller_;
  std::unique_ptr<UtilityTaskRunner> utility_task_runner_;
  ProviderDefaultModels default_models_;
  holonight_config::ProviderState provider_state_;
  QHash<QString, QString> instance_default_models_;
  QHash<QString, bool> instance_models_loaded_;
  std::shared_ptr<holonight_domain::Conversation> conversation_;
  MessageListModel* message_model_;                 // child QObject, parented to `this`
  ConversationListModel* conversation_list_model_;  // child QObject, parented to `this`

  QVariantList available_providers_;
  QStringList available_model_names_;
  holonight_domain::ModelId selected_model_id_;
  QHash<QString, QString> remembered_models_;
  ProviderStatus selected_provider_status_ = ProviderStatus::Idle;
  QString error_message_;
  QString provider_status_message_;
  QString input_text_;
  QString active_conversation_id_;
  holonight_domain::TitleSource current_title_source_ = holonight_domain::TitleSource::Fallback;
  QString persistence_status_message_;
  bool persistence_enabled_ = false;
  bool cached_can_send_ = false;
  bool cached_can_regenerate_ = false;
  bool cached_is_streaming_ = false;
  bool messages_ready_ = false;
  bool conversation_materialized_ = false;

  // Count of conversation_->messages() already INSERTed into the repository. Tool-calling turns
  // append multiple Messages (invocation, result, next placeholder) between StreamEvents with no
  // dedicated persistence call of their own -- onStreamEvent() uses this to INSERT any that are
  // new before UPDATE-ing/attaching usage to the newest one (see syncNewMessagesIntoModel()).
  std::size_t persisted_message_count_ = 0;

  // Set once each provider's startup refresh succeeds. Guards
  // syncAvailableModels()'s "selection no longer valid, fall back to first model" branch: without
  // this, a selection restored from a persisted conversation's last_model_id (adoptConversation())
  // could be clobbered by whichever provider's async refresh happens to finish first, since that
  // provider's combined model list is still missing the other provider's (not-yet-loaded) models.
  bool ollama_models_loaded_ = false;
  bool openai_models_loaded_ = false;
  bool anthropic_models_loaded_ = false;
  bool google_models_loaded_ = false;

  // Canonical message cursor for MessageListModel sync. This tracks how many entries in
  // conversation_->messages() are already reflected in message_model_ (with one- or many-to-one
  // projection through insertProjector), which is the correct anchor for the next in-place update.
  std::size_t synced_message_count_ = 0;
};

}  // namespace holonight_application
