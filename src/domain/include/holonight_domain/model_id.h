#pragma once

#include <QString>

namespace holonight_domain {

struct ModelId {
  QString provider_id{};
  QString model_name{};

  friend bool operator==(const ModelId&, const ModelId&) = default;
};

}  // namespace holonight_domain
