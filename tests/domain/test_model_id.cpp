#include "holonight_domain/model_id.h"

#include <QString>

#include <gtest/gtest.h>

namespace holonight_domain {
namespace {

TEST(ModelId, ConstructionAndAccess) {
  const ModelId model{.provider_id = QString("ollama"), .model_name = QString("llama3")};
  EXPECT_EQ(model.provider_id, QString("ollama"));
  EXPECT_EQ(model.model_name, QString("llama3"));
}

TEST(ModelId, EqualityWithIdenticalFields) {
  const ModelId lhs{.provider_id = QString("ollama"), .model_name = QString("llama3")};
  const ModelId rhs{.provider_id = QString("ollama"), .model_name = QString("llama3")};
  EXPECT_EQ(lhs, rhs);
}

TEST(ModelId, InequalityWithDifferingProviderId) {
  const ModelId lhs{.provider_id = QString("ollama"), .model_name = QString("llama3")};
  const ModelId rhs{.provider_id = QString("openai"), .model_name = QString("llama3")};
  EXPECT_NE(lhs, rhs);
}

TEST(ModelId, InequalityWithDifferingModelName) {
  const ModelId lhs{.provider_id = QString("ollama"), .model_name = QString("llama3")};
  const ModelId rhs{.provider_id = QString("ollama"), .model_name = QString("mistral")};
  EXPECT_NE(lhs, rhs);
}

}  // namespace
}  // namespace holonight_domain
