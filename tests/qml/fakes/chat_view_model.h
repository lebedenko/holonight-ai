#pragma once

#include <QAbstractListModel>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

class FakeConversationModel final : public QAbstractListModel {
  Q_OBJECT
 public:
  int rowCount(const QModelIndex& = {}) const override { return 0; }
  QVariant data(const QModelIndex&, int) const override { return {}; }
  Q_INVOKABLE int visibleCount(bool, const QString&) const { return 0; }
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
  enum ProviderStatus { Idle, Checking, LoadingModels, Connected, SetupRequired, Unavailable, Error };
  Q_ENUM(ProviderStatus)
  QVariantList available_providers;
  QStringList available_model_names;
  QString selected_provider_id;
  QString selected_model_name;
  int selected_provider_status = Idle;
  QString provider_status_message;
  QString error_message;
  QString persistence_status_message;
  bool messages_ready = true;
  QVariantList messages;
  FakeConversationModel conversations;
  QAbstractItemModel* conversationList() { return &conversations; }
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
