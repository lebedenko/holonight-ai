#pragma once

#include <QObject>
#include <QString>

#include <holonight_config/config_repository.h>

namespace holonight_credentials {
class CredentialStore;
}

namespace holonight_application {

class ProviderDraftSession;
class ProviderInstanceRegistry;

class ProviderDraftCommitter : public QObject {
  Q_OBJECT

 public:
  explicit ProviderDraftCommitter(ProviderInstanceRegistry* registry,
                                  holonight_credentials::CredentialStore* credential_store,
                                  holonight_config::ConfigRepository config_repository, QObject* parent = nullptr);

  [[nodiscard]] bool save(ProviderDraftSession* draft);
  [[nodiscard]] bool retryCredential();
  [[nodiscard]] bool credentialRetryPending() const;
  [[nodiscard]] QString error() const;

 Q_SIGNALS:
  void errorChanged();
  void credentialRetryPendingChanged();
  void saveCompleted();

 private:
  void startCredentialOperation();
  void finishCredentialOperation(const QString& provider_id);
  void failCredentialOperation(const QString& reason);
  void setError(QString error);

  ProviderInstanceRegistry* registry_;
  holonight_credentials::CredentialStore* credential_store_;
  holonight_config::ConfigRepository config_repository_;
  ProviderDraftSession* pending_draft_ = nullptr;
  QString pending_provider_id_;
  QString error_;
  bool credential_operation_in_progress_ = false;
  bool credential_retry_pending_ = false;
};

}  // namespace holonight_application
