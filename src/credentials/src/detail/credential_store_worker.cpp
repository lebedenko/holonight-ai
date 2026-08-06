// glib/gio headers (pulled in transitively by <libsecret/secret.h>) declare a struct field
// literally named `signals` (GDBusSignalInfo **signals in gdbusintrospection.h). Qt's <QObject>
// normally defines `signals`/`slots`/`emit` as keyword macros, which would corrupt that
// declaration wherever glib headers are parsed afterward. QT_NO_KEYWORDS disables those bare
// macros for this translation unit; this file uses Q_EMIT instead of `emit` below (Q_SIGNALS/
// Q_SLOTS in the header are unaffected — those are always defined). This avoids depending on
// include order, which .clang-format's IncludeBlocks:Regroup would not preserve anyway (it always
// sorts quoted project headers before angle-bracket system headers).
#define QT_NO_KEYWORDS

#include "holonight_credentials/detail/credential_store_worker.h"

#include <QDebug>
#include <QString>
#include <QStringList>

#include <libsecret/secret.h>

namespace holonight_credentials::detail {

namespace {

// Mirrors the schema used throughout this file — one attribute, provider_id, matching
// REQ-C-005's single-key data model. SECRET_SCHEMA_NONE (not SECRET_SCHEMA_DONT_MATCH_NAME)
// because loadKnownProviderIds() searches with an empty attribute table, which
// SECRET_SCHEMA_DONT_MATCH_NAME would reject with SECRET_ERROR_EMPTY_TABLE. Trailing `reserved*`
// fields are libsecret-private and intentionally left at their zero default (designated
// initializers may omit trailing aggregate members).
const SecretSchema* credentialSchema() {
  static const SecretSchema schema = {
      .name = "ai.holonight.ProviderCredential",
      .flags = SECRET_SCHEMA_NONE,
      .attributes =
          {
              {.name = "provider_id", .type = SECRET_SCHEMA_ATTRIBUTE_STRING},
              {.name = nullptr, .type = static_cast<SecretSchemaAttributeType>(0)},
          },
  };
  return &schema;
}

QByteArray labelFor(const QString& providerId) {
  return QStringLiteral("Holonight AI: %1 API key").arg(providerId).toUtf8();
}

// Builds the single-attribute lookup table libsecret's *v_sync functions expect, avoiding the
// vararg secret_password_*_sync overloads entirely (cppcoreguidelines-pro-type-vararg). Caller
// owns the returned table and must g_hash_table_unref() it.
GHashTable* attributesFor(const QString& providerId) {
  GHashTable* attributes = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
  g_hash_table_insert(attributes, g_strdup("provider_id"), g_strdup(providerId.toUtf8().constData()));
  return attributes;
}

}  // namespace

CredentialStoreWorker::CredentialStoreWorker(QObject* parent) : QObject(parent) {}

void CredentialStoreWorker::handleLibsecretError(struct _GError* error) {
  const QString reason = QStringLiteral("Failed to reach Secret Service: %1")
                             .arg(QString::fromUtf8(error != nullptr ? error->message : ""));
  if (error != nullptr) {
    g_error_free(error);
  }
  if (!available_) {
    return;
  }
  available_ = false;
  qWarning().noquote() << reason;
  Q_EMIT unavailable(reason);
}

void CredentialStoreWorker::loadKnownProviderIds() {
  GHashTable* attributes = g_hash_table_new(g_str_hash, g_str_equal);
  GError* error = nullptr;
  GList* items =
      secret_service_search_sync(nullptr, credentialSchema(), attributes, SECRET_SEARCH_NONE, nullptr, &error);
  g_hash_table_unref(attributes);

  if (error != nullptr) {
    handleLibsecretError(error);
    return;
  }

  QStringList providerIds;
  for (GList* node = items; node != nullptr; node = node->next) {
    auto* item = static_cast<SecretItem*>(node->data);
    GHashTable* itemAttributes = secret_item_get_attributes(item);
    const auto* providerId = static_cast<const gchar*>(g_hash_table_lookup(itemAttributes, "provider_id"));
    if (providerId != nullptr) {
      providerIds.append(QString::fromUtf8(providerId));
    }
    g_hash_table_unref(itemAttributes);
  }
  g_list_free_full(items, g_object_unref);

  Q_EMIT knownProviderIdsLoaded(providerIds);
}

void CredentialStoreWorker::store(const QString& providerId, const QString& secret) {
  if (available_) {
    GHashTable* attributes = attributesFor(providerId);
    GError* error = nullptr;
    secret_password_storev_sync(credentialSchema(), attributes, SECRET_COLLECTION_DEFAULT,
                                labelFor(providerId).constData(), secret.toUtf8().constData(), nullptr, &error);
    g_hash_table_unref(attributes);
    if (error != nullptr) {
      handleLibsecretError(error);
    }
  }
  Q_EMIT storeCompleted(providerId);
}

void CredentialStoreWorker::retrieve(const QString& providerId) {
  if (!available_) {
    Q_EMIT retrieveCompleted(providerId, false, QString());
    return;
  }

  GHashTable* attributes = attributesFor(providerId);
  GError* error = nullptr;
  gchar* password = secret_password_lookupv_sync(credentialSchema(), attributes, nullptr, &error);
  g_hash_table_unref(attributes);

  if (error != nullptr) {
    handleLibsecretError(error);
    Q_EMIT retrieveCompleted(providerId, false, QString());
    return;
  }

  if (password == nullptr) {
    // Genuine, ordinary miss — libsecret's own contract guarantees NULL secret with NULL error
    // for a clean miss, never the reverse. available_ is untouched here (REQ-F-006).
    Q_EMIT retrieveCompleted(providerId, false, QString());
    return;
  }

  const QString secret = QString::fromUtf8(password);
  secret_password_free(password);
  Q_EMIT retrieveCompleted(providerId, true, secret);
}

void CredentialStoreWorker::remove(const QString& providerId) {
  if (available_) {
    GHashTable* attributes = attributesFor(providerId);
    GError* error = nullptr;
    // Return value (whether anything was actually removed) is intentionally discarded —
    // REQ-F-003 only requires removeCompleted() either way.
    secret_password_clearv_sync(credentialSchema(), attributes, nullptr, &error);
    g_hash_table_unref(attributes);
    if (error != nullptr) {
      handleLibsecretError(error);
    }
  }
  Q_EMIT removeCompleted(providerId);
}

}  // namespace holonight_credentials::detail
