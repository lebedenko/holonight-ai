#include "holonight_persistence/usage_record.h"

#include <utility>

namespace holonight_persistence {

UsageRecord toUsageRecord(QString conversationId, QString messageId, QString modelIdentifier,
                          const holonight_domain::Usage& usage) {
  return UsageRecord{
      .conversation_id = std::move(conversationId),
      .message_id = std::move(messageId),
      .model_identifier = std::move(modelIdentifier),
      .input_tokens = usage.input_tokens,
      .output_tokens = usage.output_tokens,
      .reasoning_tokens = usage.reasoning_tokens,
      .cache_creation_tokens = usage.cache_creation_tokens,
      .cache_read_tokens = usage.cache_read_tokens,
      .total_tokens = usage.total_tokens,
      .duration_ms = usage.duration_ms,
      .ollama_total_duration_ns = usage.ollama_total_duration_ns,
      .ollama_load_duration_ns = usage.ollama_load_duration_ns,
      .ollama_prompt_eval_duration_ns = usage.ollama_prompt_eval_duration_ns,
      .ollama_eval_duration_ns = usage.ollama_eval_duration_ns,
  };
}

}  // namespace holonight_persistence
