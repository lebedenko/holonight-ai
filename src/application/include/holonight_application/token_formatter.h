#pragma once

#include <QObject>
#include <QString>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

namespace holonight_application {

// REQ-F-005. QML-callable wrapper around the plain-C++ formatTokenCount() free function
// (token_formatting.h) -- see DESIGN.md Key Decision 4.1 for why the actual logic lives outside
// any QObject (GTest-able with zero meta-object overhead) while this singleton exists purely to
// make it reachable from QML as `TokenFormatter.formatTokenCount(...)`.
class TokenFormatter : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON
  QML_UNCREATABLE("Stateless formatting helper; call its methods directly, do not instantiate")

 public:
  using QObject::QObject;

  Q_INVOKABLE static QString formatTokenCount(const QVariant& tokens);
};

}  // namespace holonight_application
