#pragma once

#include <QMetaType>
#include <QString>
#include <QtGlobal>

#include <holonight_domain/holonight_domain.h>
#include <optional>

namespace holonight_persistence {

// Persistence-layer representation of one row of the `usage` table (REQ-F-009/REQ-NF-002).
// conversation_id/message_id/model_identifier mirror the table's NOT NULL columns; every
// token/timing field mirrors a nullable column and stays std::optional so "provider did not
// report this" round-trips as SQL NULL, never 0.
struct UsageRecord {
  QString conversation_id;
  QString message_id;
  QString model_identifier;
  std::optional<int> input_tokens;
  std::optional<int> output_tokens;
  std::optional<int> reasoning_tokens;
  std::optional<int> cache_creation_tokens;
  std::optional<int> cache_read_tokens;
  std::optional<int> total_tokens;
  std::optional<qint64> duration_ms;
  std::optional<qint64> ollama_total_duration_ns;
  std::optional<qint64> ollama_load_duration_ns;
  std::optional<qint64> ollama_prompt_eval_duration_ns;
  std::optional<qint64> ollama_eval_duration_ns;

  friend bool operator==(const UsageRecord&, const UsageRecord&) = default;
};

// Builds the persisted row from the domain Usage plus the identifiers (REQ-F-009) -- the only
// place Usage's fields are copied into the persistence-layer shape.
[[nodiscard]] UsageRecord toUsageRecord(QString conversationId, QString messageId, QString modelIdentifier,
                                        const holonight_domain::Usage& usage);

}  // namespace holonight_persistence

Q_DECLARE_METATYPE(holonight_persistence::UsageRecord)
