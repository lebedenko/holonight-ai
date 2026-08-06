#pragma once

#include <QHash>
#include <QObject>
#include <QString>

#include <cstdint>
#include <functional>
#include <holonight_credentials/credential_store.h>
#include <holonight_domain/model_id.h>
#include <vector>

namespace holonight_application {

enum class CredentialPolicy : std::uint8_t { Optional, Required };
enum class ProviderReadiness : std::uint8_t { Unresolved, Ready, MissingCredential, Unavailable };
enum class ProviderRefreshState : std::uint8_t { NotStarted, InProgress, Succeeded, Failed };

struct ProviderRuntimeOperations {
  std::function<void(const QString&)> set_credential;
  std::function<void(const std::function<void()>&, const std::function<void(const QString&)>&)> refresh;
  std::function<const std::vector<holonight_domain::ModelId>&()> models;
  std::function<void()> persist_models;
};

class ProviderRuntimeCoordinator : public QObject {
  Q_OBJECT

 public:
  explicit ProviderRuntimeCoordinator(holonight_credentials::CredentialStore* credential_store,
                                      QObject* parent = nullptr);

  void registerProvider(QString provider_id, QString display_name, CredentialPolicy credential_policy,
                        ProviderRuntimeOperations operations);
  [[nodiscard]] bool unregisterProvider(const QString& provider_id);
  [[nodiscard]] bool contains(const QString& provider_id) const;
  // Idempotent for each registration: repeated calls do not retrieve credentials or start
  // another automatic refresh.
  void prepare(const QString& provider_id);
  void forceRefresh(const QString& provider_id);
  void setEnabled(const QString& provider_id, bool enabled);
  [[nodiscard]] bool isEnabled(const QString& provider_id) const;

  [[nodiscard]] ProviderReadiness readiness(const QString& provider_id) const;
  [[nodiscard]] ProviderRefreshState refreshState(const QString& provider_id) const;
  [[nodiscard]] QString statusMessage(const QString& provider_id) const;
  [[nodiscard]] bool isReady(const QString& provider_id) const;

 Q_SIGNALS:
  void providerChanged(QString providerId);

 private:
  struct Registration {
    QString display_name;
    CredentialPolicy credential_policy = CredentialPolicy::Required;
    ProviderRuntimeOperations operations;
    ProviderReadiness readiness = ProviderReadiness::Unresolved;
    ProviderRefreshState refresh_state = ProviderRefreshState::NotStarted;
    QString status_message;
    bool prepare_pending = false;
    bool refresh_requested = false;
    bool enabled = true;
    std::uint64_t refresh_generation = 0;
  };

  void resolveCredential(const QString& provider_id);
  void startRefresh(const QString& provider_id, bool force);
  void onCredentialRetrieved(const QString& provider_id, bool found, const QString& secret);
  void onCredentialStoreReady();
  void onCredentialStoreUnavailable(const QString& reason);

  holonight_credentials::CredentialStore* credential_store_;
  QHash<QString, Registration> registrations_;
};

}  // namespace holonight_application
