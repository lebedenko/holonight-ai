#pragma once

#include "holonight_application/tools/tool_contracts.h"

#include <QVariant>

namespace holonight_application {

class GenericToolPresenter : public IToolPresenter {
 public:
  [[nodiscard]] ToolPresentation present(const holonight_domain::ToolInvocation& invocation) const override;
};

class ListFilesPresenter : public IToolPresenter {
 public:
  [[nodiscard]] ToolPresentation present(const holonight_domain::ToolInvocation& invocation) const override;
};

}  // namespace holonight_application
