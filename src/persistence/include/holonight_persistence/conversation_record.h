#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QString>

#include <holonight_domain/holonight_domain.h>
#include <holonight_persistence/usage_record.h>
#include <optional>
#include <vector>

namespace holonight_persistence {

inline const QString kDefaultConversationTitle = QStringLiteral("New Chat");

struct ConversationSummary {
  QString id;
  QString title;
  QDateTime created_at;
  QDateTime updated_at;
  std::optional<holonight_domain::ModelId> last_model_id;
  holonight_domain::TitleSource title_source = holonight_domain::TitleSource::Fallback;
  std::optional<QDateTime> pinned_at;

  friend bool operator==(const ConversationSummary&, const ConversationSummary&) = default;
};

struct LoadedConversation {
  ConversationSummary summary;
  std::vector<holonight_domain::Message> messages;  // chronological, ascending created_at

  friend bool operator==(const LoadedConversation&, const LoadedConversation&) = default;
};

// Idempotent. Every type used as a queued cross-thread signal parameter must be registered
// before the first such emission, or the connection silently drops the call with a runtime
// warning, not a compile error.
void registerMetaTypes();

}  // namespace holonight_persistence

Q_DECLARE_METATYPE(holonight_persistence::ConversationSummary)
Q_DECLARE_METATYPE(holonight_persistence::LoadedConversation)
Q_DECLARE_METATYPE(holonight_domain::ModelId)
