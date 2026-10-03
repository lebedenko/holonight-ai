// Fixture state is visible to derived test cases and deterministic assertions.
#pragma once

#include <QAbstractListModel>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include <cstdint>

class FakeConversationModel final : public QAbstractListModel {
  Q_OBJECT
 public:
  [[nodiscard]] int rowCount([[maybe_unused]] const QModelIndex& parent = {}) const override { return 0; }
  [[nodiscard]] QVariant data([[maybe_unused]] const QModelIndex& index, [[maybe_unused]] int role) const override {
    return {};
  }
  Q_INVOKABLE [[nodiscard]] static int visibleCount([[maybe_unused]] bool pinned,
                                                    [[maybe_unused]] const QString& query) {
    return 0;
  }
};

class FakeChatViewModel final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool canSend READ canSend WRITE setCanSend NOTIFY canSendChanged)
  Q_PROPERTY(bool canRegenerate READ canRegenerate CONSTANT)
  Q_PROPERTY(bool isStreaming READ isStreaming WRITE setIsStreaming NOTIFY isStreamingChanged)
  Q_PROPERTY(QString inputText READ inputText WRITE setInputText NOTIFY inputTextChanged)

  Q_PROPERTY(QVariantList availableProviders MEMBER available_providers NOTIFY availableProvidersChanged)
  Q_PROPERTY(QStringList availableModelNames MEMBER available_model_names NOTIFY availableModelNamesChanged)
  Q_PROPERTY(QString selectedProviderId MEMBER selected_provider_id NOTIFY selectedProviderIdChanged)
  Q_PROPERTY(QString selectedModelName MEMBER selected_model_name NOTIFY selectedModelNameChanged)
  Q_PROPERTY(int selectedProviderStatus MEMBER selected_provider_status CONSTANT)
  Q_PROPERTY(QString providerStatusMessage MEMBER provider_status_message CONSTANT)
  Q_PROPERTY(QString errorMessage MEMBER error_message CONSTANT)
  Q_PROPERTY(QString persistenceStatusMessage MEMBER persistence_status_message CONSTANT)
  Q_PROPERTY(bool messagesReady MEMBER messages_ready CONSTANT)
  Q_PROPERTY(QVariantList messages MEMBER messages NOTIFY messagesChanged)
  Q_PROPERTY(QAbstractItemModel* conversationList READ conversationList CONSTANT)
  Q_PROPERTY(QString activeConversationId MEMBER active_conversation_id CONSTANT)
 public:
  enum ProviderStatus : std::uint8_t {  // NOLINT(cppcoreguidelines-use-enum-class): Matches reflected production
                                        // constants.
    Idle,
    Checking,
    LoadingModels,
    Connected,
    SetupRequired,
    Unavailable,
    Error
  };
  Q_ENUM(ProviderStatus)
  // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
  QVariantList available_providers;
  // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
  QStringList available_model_names;
  // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
  QString selected_provider_id;
  // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
  QString selected_model_name;
  // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
  int selected_provider_status = Idle;
  // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
  QString provider_status_message;
  // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
  QString error_message;
  // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
  QString persistence_status_message;
  // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
  bool messages_ready = true;
  // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
  QVariantList messages;
  // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
  FakeConversationModel conversations;
  QAbstractItemModel* conversationList() { return &conversations; }
  // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
  QString active_conversation_id;
  [[nodiscard]] bool canSend() const { return can_send_; }
  [[nodiscard]] static bool canRegenerate() { return false; }
  [[nodiscard]] bool isStreaming() const { return is_streaming_; }
  [[nodiscard]] QString inputText() const { return input_text_; }
  [[nodiscard]] int sendCount() const { return send_count_; }
  [[nodiscard]] QString lastSentText() const { return last_sent_text_; }

  void setCanSend(bool can_send) {
    if (can_send_ == can_send) {
      return;
    }
    can_send_ = can_send;
    Q_EMIT canSendChanged();
  }

  void setIsStreaming(bool is_streaming) {
    if (is_streaming_ == is_streaming) {
      return;
    }
    is_streaming_ = is_streaming;
    Q_EMIT isStreamingChanged();
  }

  void setInputText(const QString& input_text) {
    if (input_text_ == input_text) {
      return;
    }
    input_text_ = input_text;
    Q_EMIT inputTextChanged();
  }

  Q_INVOKABLE void send(const QString& text) {
    ++send_count_;
    last_sent_text_ = text;
    setInputText({});
  }

  Q_INVOKABLE void regenerate() {}

 Q_SIGNALS:
  void availableProvidersChanged();
  void availableModelNamesChanged();
  void selectedProviderIdChanged();
  void selectedModelNameChanged();
  void messagesChanged();
  void canSendChanged();
  void isStreamingChanged();
  void inputTextChanged();

 private:
  bool can_send_ = true;
  bool is_streaming_ = false;
  QString input_text_;
  QString last_sent_text_;
  int send_count_ = 0;
};
