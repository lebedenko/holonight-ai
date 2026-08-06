#pragma once

#include <QObject>
#include <QString>

#include <holonight_config/config_repository.h>

namespace holonight_credentials {
class CredentialStore;
}

namespace holonight_application {

class ProviderAdapterRouter;
class ProviderInstanceRegistry;
class ProviderRuntimeCoordinator;

class ProviderInstanceDeleter : public QObject {
  Q_OBJECT

 public:
  explicit ProviderInstanceDeleter(ProviderInstanceRegistry* registry, ProviderAdapterRouter* router,
                                   ProviderRuntimeCoordinator* runtime_coordinator,
                                   holonight_credentials::CredentialStore* credential_store,
                                   holonight_config::ConfigRepository config_repository, QObject* parent = nullptr);

  [[nodiscard]] bool remove(const QString& instance_id, bool confirmed);
  [[nodiscard]] bool retryCredentialCleanup();
  [[nodiscard]] bool credentialRetryPending() const;
  [[nodiscard]] QString error() const;

 Q_SIGNALS:
  void errorChanged();
  void credentialRetryPendingChanged();
  void deletionCompleted(QString instanceId);

 private:
  void finishCredentialCleanup(const QString& provider_id);
  void failCredentialCleanup(const QString& reason);
  void startCredentialCleanup();
  void setError(QString error);

  ProviderInstanceRegistry* registry_;
  ProviderAdapterRouter* router_;
  ProviderRuntimeCoordinator* runtime_coordinator_;
  holonight_credentials::CredentialStore* credential_store_;
  holonight_config::ConfigRepository config_repository_;
  QString pending_credential_id_;
  QString error_;
  bool credential_operation_in_progress_ = false;
  bool credential_retry_pending_ = false;
};

}  // namespace holonight_application
