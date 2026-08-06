#include "holonight_application/provider_settings_controller_base.h"

#include "holonight_application/provider_draft_session.h"

#include <utility>

namespace holonight_application {

using holonight_credentials::CredentialStore;
using holonight_domain::ModelId;

ProviderSettingsControllerBase::ProviderSettingsControllerBase(QString provider_id, CredentialStore* credential_store,
                                                               ProviderOperations operations,
                                                               bool select_first_available_model, QObject* parent)
    : QObject(parent),
      provider_id_(std::move(provider_id)),
      credential_store_(credential_store),
      operations_(std::move(operations)),
      select_first_available_model_(select_first_available_model),
      credential_store_available_(credential_store_->isAvailable()) {
  connect(credential_store_, &CredentialStore::retrieveCompleted, this,
          &ProviderSettingsControllerBase::onCredentialRetrieved);
  connect(credential_store_, &CredentialStore::unavailable, this,
          &ProviderSettingsControllerBase::onCredentialStoreUnavailable);
}

QString ProviderSettingsControllerBase::baseUrl() const { return base_url_; }

void ProviderSettingsControllerBase::setBaseUrl(QString url) {
  if (base_url_ == url) {
    return;
  }
  base_url_ = std::move(url);
  emit baseUrlChanged();
}

QString ProviderSettingsControllerBase::defaultModel() const { return default_model_; }

void ProviderSettingsControllerBase::setDefaultModel(QString model) {
  if (default_model_ == model) {
    return;
  }
  default_model_ = std::move(model);
  emit defaultModelChanged();
}

QString ProviderSettingsControllerBase::authToken() const { return auth_token_; }

void ProviderSettingsControllerBase::setAuthToken(QString token) {
  if (auth_token_ == token) {
    return;
  }
  auth_token_ = std::move(token);
  auth_token_edited_since_retrieve_ = true;
  if (draft_session_ != nullptr) {
    if (auth_token_.isEmpty()) {
      draft_session_->removeCredential();
    } else {
      draft_session_->setCredential(auth_token_);
    }
  }
  emit authTokenChanged();
}

void ProviderSettingsControllerBase::retargetDraft(ProviderDraftSession* draft_session) {
  draft_session_ = draft_session;
  if (draft_session_ != nullptr) {
    provider_id_ = draft_session_->editable().id;
  }
}

ProviderDraftSession* ProviderSettingsControllerBase::draftSession() const { return draft_session_; }

bool ProviderSettingsControllerBase::hasStoredToken() const { return has_stored_credential_; }

void ProviderSettingsControllerBase::setHasStoredToken(bool value) {
  if (has_stored_credential_ == value) {
    return;
  }
  has_stored_credential_ = value;
  emit hasStoredTokenChanged();
}

bool ProviderSettingsControllerBase::credentialStoreAvailable() const { return credential_store_available_; }

void ProviderSettingsControllerBase::setCredentialStoreAvailable(bool value) {
  if (credential_store_available_ == value) {
    return;
  }
  credential_store_available_ = value;
  emit credentialStoreAvailableChanged();
}

bool ProviderSettingsControllerBase::credentialOperationInProgress() const { return credential_operation_in_progress_; }

void ProviderSettingsControllerBase::setCredentialOperationInProgress(bool value) {
  if (credential_operation_in_progress_ == value) {
    return;
  }
  credential_operation_in_progress_ = value;
  emit credentialOperationInProgressChanged();
}

QStringList ProviderSettingsControllerBase::availableModelNames() const { return available_model_names_; }

void ProviderSettingsControllerBase::setAvailableModelNames(QStringList names) {
  if (available_model_names_ == names) {
    return;
  }
  available_model_names_ = std::move(names);
  emit availableModelNamesChanged();
}

bool ProviderSettingsControllerBase::modelRefreshInProgress() const { return model_refresh_in_progress_; }

void ProviderSettingsControllerBase::setModelRefreshInProgress(bool value) {
  if (model_refresh_in_progress_ == value) {
    return;
  }
  model_refresh_in_progress_ = value;
  emit modelRefreshInProgressChanged();
}

QString ProviderSettingsControllerBase::modelRefreshError() const { return model_refresh_error_; }

void ProviderSettingsControllerBase::setModelRefreshError(QString message) {
  if (model_refresh_error_ == message) {
    return;
  }
  model_refresh_error_ = std::move(message);
  emit modelRefreshErrorChanged();
}

bool ProviderSettingsControllerBase::testConnectionInProgress() const { return test_connection_in_progress_; }

void ProviderSettingsControllerBase::setTestConnectionInProgress(bool value) {
  if (test_connection_in_progress_ == value) {
    return;
  }
  test_connection_in_progress_ = value;
  emit testConnectionInProgressChanged();
}

QString ProviderSettingsControllerBase::testConnectionStatus() const { return test_connection_status_; }

void ProviderSettingsControllerBase::setTestConnectionStatus(QString status) {
  if (test_connection_status_ == status) {
    return;
  }
  test_connection_status_ = std::move(status);
  emit testConnectionStatusChanged();
}

QString ProviderSettingsControllerBase::testConnectionMessage() const { return test_connection_message_; }

void ProviderSettingsControllerBase::setTestConnectionMessage(QString message) {
  if (test_connection_message_ == message) {
    return;
  }
  test_connection_message_ = std::move(message);
  emit testConnectionMessageChanged();
}

QString ProviderSettingsControllerBase::connectionStatus() const { return connection_status_; }

void ProviderSettingsControllerBase::setConnectionStatus(QString status) {
  if (connection_status_ == status) {
    return;
  }
  connection_status_ = std::move(status);
  emit connectionStatusChanged();
}

QString ProviderSettingsControllerBase::saveNotice() const { return save_notice_; }

QString ProviderSettingsControllerBase::saveNoticeStatus() const { return save_notice_status_; }

void ProviderSettingsControllerBase::setSaveNotice(QString notice, QString status) {
  if (save_notice_ == notice && save_notice_status_ == status) {
    return;
  }
  save_notice_ = std::move(notice);
  save_notice_status_ = std::move(status);
  emit saveNoticeChanged();
}

QString ProviderSettingsControllerBase::normalizeBaseUrl(QString url) {
  if (url.endsWith(QLatin1Char('/'))) {
    url.chop(1);
  }
  return url;
}

void ProviderSettingsControllerBase::beginLoad() {
  operations_.configure_probe(normalizeBaseUrl(base_url_), auth_token_);
  auth_token_edited_since_retrieve_ = false;
  setCredentialOperationInProgress(true);
  credential_store_->retrieve(provider_id_);
  setCredentialStoreAvailable(credential_store_->isAvailable());
}

void ProviderSettingsControllerBase::refreshModels() {
  operations_.configure_probe(normalizeBaseUrl(base_url_), auth_token_);
  setModelRefreshInProgress(true);
  operations_.refresh_probe(
      [this] {
        setModelRefreshInProgress(false);
        setModelRefreshError(QString());

        QStringList model_names;
        const auto& models = operations_.probe_models();
        model_names.reserve(static_cast<int>(models.size()));
        for (const ModelId& model : models) {
          model_names.append(model.model_name);
        }
        if (select_first_available_model_ && !model_names.contains(default_model_)) {
          setDefaultModel(model_names.isEmpty() ? QString() : model_names.front());
        }
        setAvailableModelNames(std::move(model_names));
        setConnectionStatus(QStringLiteral("connected"));
      },
      [this](const QString& reason) {
        setModelRefreshInProgress(false);
        setModelRefreshError(tr("Failed to fetch models: %1").arg(reason));
        setConnectionStatus(QStringLiteral("error"));
      });
}

void ProviderSettingsControllerBase::testConnection() {
  operations_.configure_probe(normalizeBaseUrl(base_url_), auth_token_);
  setTestConnectionInProgress(true);
  operations_.refresh_probe(
      [this] {
        setTestConnectionInProgress(false);
        setTestConnectionStatus(QStringLiteral("success"));
        setTestConnectionMessage(tr("Connection succeeded."));
        setConnectionStatus(QStringLiteral("connected"));
      },
      [this](const QString& reason) {
        setTestConnectionInProgress(false);
        setTestConnectionStatus(QStringLiteral("error"));
        setTestConnectionMessage(tr("Connection failed: %1").arg(reason));
        setConnectionStatus(QStringLiteral("error"));
      });
}

void ProviderSettingsControllerBase::clearTransientState() {
  setAuthToken(QString());
  setModelRefreshError(QString());
  setTestConnectionStatus(QStringLiteral("idle"));
}

void ProviderSettingsControllerBase::onCredentialRetrieved(const QString& provider_id, bool found,
                                                           const QString& secret) {
  if (provider_id != provider_id_) {
    return;
  }
  setHasStoredToken(found);
  if (found && !auth_token_edited_since_retrieve_) {
    auth_token_ = secret;
    emit authTokenChanged();
    operations_.configure_probe(normalizeBaseUrl(base_url_), secret);
  }
  setCredentialOperationInProgress(false);
  refreshModels();
}

void ProviderSettingsControllerBase::onCredentialStoreUnavailable(const QString& reason) {
  setCredentialStoreAvailable(false);
  setCredentialOperationInProgress(false);
  setSaveNotice(tr("Secret storage unavailable: %1").arg(reason), QStringLiteral("error"));
}

}  // namespace holonight_application
