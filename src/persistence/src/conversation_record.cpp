#include "holonight_persistence/conversation_record.h"

namespace holonight_persistence {

void registerMetaTypes() {
  qRegisterMetaType<ConversationSummary>();
  qRegisterMetaType<LoadedConversation>();
  qRegisterMetaType<holonight_domain::ModelId>();
  qRegisterMetaType<UsageRecord>();
}

}  // namespace holonight_persistence
