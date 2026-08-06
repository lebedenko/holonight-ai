#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <holonight_credentials/credential_store.h>
#include <holonight_domain/holonight_domain.h>
#include <vector>

namespace holonight_application {

class ProviderDraftSession;

class ProviderSettingsControllerBase : public QObject {
  Q_OBJECT

  Q_PROPERTY(QString baseUrl READ baseUrl WRITE setBaseUrl NOTIFY baseUrlChanged)
  Q_PROPERTY(QString defaultModel READ defaultModel WRITE setDefaultModel NOTIFY defaultModelChanged)
  Q_PROPERTY(QString authToken READ authToken WRITE setAuthToken NOTIFY authTokenChanged)
  Q_PROPERTY(bool hasStoredToken READ hasStoredToken NOTIFY hasStoredTokenChanged)
  Q_PROPERTY(bool credentialStoreAvailable READ credentialStoreAvailable NOTIFY credentialStoreAvailableChanged)
  Q_PROPERTY(
      bool credentialOperationInProgress READ credentialOperationInProgress NOTIFY credentialOperationInProgressChanged)
  Q_PROPERTY(QStringList availableModelNames READ availableModelNames NOTIFY availableModelNamesChanged)
  Q_PROPERTY(bool modelRefreshInProgress READ modelRefreshInProgress NOTIFY modelRefreshInProgressChanged)
  Q_PROPERTY(QString modelRefreshError READ modelRefreshError NOTIFY modelRefreshErrorChanged)
  Q_PROPERTY(bool testConnectionInProgress READ testConnectionInProgress NOTIFY testConnectionInProgressChanged)
  Q_PROPERTY(QString testConnectionStatus READ testConnectionStatus NOTIFY testConnectionStatusChanged)
  Q_PROPERTY(QString testConnectionMessage READ testConnectionMessage NOTIFY testConnectionMessageChanged)
  Q_PROPERTY(QString saveNotice READ saveNotice NOTIFY saveNoticeChanged)
  Q_PROPERTY(QString saveNoticeStatus READ saveNoticeStatus NOTIFY saveNoticeChanged)

 public:
  struct ProviderOperations {
    std::function<void(const QString&, const QString&)> configure_probe;
    std::function<void(const std::function<void()>&, const std::function<void(const QString&)>&)> refresh_probe;
    std::function<const std::vector<holonight_domain::ModelId>&()> probe_models;
  };

  [[nodiscard]] QString baseUrl() const;
  void setBaseUrl(QString url);
  [[nodiscard]] QString defaultModel() const;
  void setDefaultModel(QString model);
  [[nodiscard]] QString authToken() const;
  void setAuthToken(QString token);
  [[nodiscard]] bool hasStoredToken() const;
  [[nodiscard]] bool credentialStoreAvailable() const;
  [[nodiscard]] bool credentialOperationInProgress() const;
  [[nodiscard]] QStringList availableModelNames() const;
  [[nodiscard]] bool modelRefreshInProgress() const;
  [[nodiscard]] QString modelRefreshError() const;
  [[nodiscard]] bool testConnectionInProgress() const;
  [[nodiscard]] QString testConnectionStatus() const;
  [[nodiscard]] QString testConnectionMessage() const;
  [[nodiscard]] QString saveNotice() const;
  [[nodiscard]] QString saveNoticeStatus() const;

  Q_INVOKABLE void refreshModels();
  Q_INVOKABLE void testConnection();

 Q_SIGNALS:
  void baseUrlChanged();
  void defaultModelChanged();
  void authTokenChanged();
  void hasStoredTokenChanged();
  void credentialStoreAvailableChanged();
  void credentialOperationInProgressChanged();
  void availableModelNamesChanged();
  void modelRefreshInProgressChanged();
  void modelRefreshErrorChanged();
  void testConnectionInProgressChanged();
  void testConnectionStatusChanged();
  void testConnectionMessageChanged();
  void saveNoticeChanged();

 protected:
  ProviderSettingsControllerBase(QString provider_id, holonight_credentials::CredentialStore* credential_store,
                                 ProviderOperations operations, bool select_first_available_model,
                                 QObject* parent = nullptr);

  [[nodiscard]] holonight_credentials::CredentialStore* credentialStore() const { return credential_store_; }
  [[nodiscard]] QString connectionStatus() const;
  [[nodiscard]] static QString normalizeBaseUrl(QString url);

  void beginLoad();
  void clearTransientState();
  void setSaveNotice(QString notice, QString status);
  void retargetDraft(ProviderDraftSession* draft_session);
  [[nodiscard]] ProviderDraftSession* draftSession() const;

 Q_SIGNALS:
  void connectionStatusChanged();

 private:
  void setHasStoredToken(bool value);
  void setCredentialStoreAvailable(bool value);
  void setCredentialOperationInProgress(bool value);
  void setAvailableModelNames(QStringList names);
  void setModelRefreshInProgress(bool value);
  void setModelRefreshError(QString message);
  void setTestConnectionInProgress(bool value);
  void setTestConnectionStatus(QString status);
  void setTestConnectionMessage(QString message);
  void setConnectionStatus(QString status);

  void onCredentialRetrieved(const QString& provider_id, bool found, const QString& secret);
  void onCredentialStoreUnavailable(const QString& reason);

  QString base_url_;
  QString default_model_;
  QString auth_token_;
  QString provider_id_;
  holonight_credentials::CredentialStore* credential_store_;
  ProviderOperations operations_;
  bool select_first_available_model_;
  bool has_stored_credential_ = false;
  bool credential_store_available_ = true;
  bool credential_operation_in_progress_ = false;
  bool auth_token_edited_since_retrieve_ = false;
  QStringList available_model_names_;
  bool model_refresh_in_progress_ = false;
  QString model_refresh_error_;
  bool test_connection_in_progress_ = false;
  QString test_connection_status_ = QStringLiteral("idle");
  QString test_connection_message_;
  QString connection_status_ = QStringLiteral("unknown");
  QString save_notice_;
  QString save_notice_status_ = QStringLiteral("idle");
  ProviderDraftSession* draft_session_ = nullptr;
};

}  // namespace holonight_application
