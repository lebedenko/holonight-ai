#include "holonight_rendering/language_alias.h"

#include <QHash>

namespace holonight_rendering::LanguageAlias {

namespace {

const QHash<QString, QString>& aliasTable() {
  static const QHash<QString, QString> table{
      {QStringLiteral("cpp"), QStringLiteral("C++")},
      {QStringLiteral("c++"), QStringLiteral("C++")},
      {QStringLiteral("cxx"), QStringLiteral("C++")},
      {QStringLiteral("js"), QStringLiteral("JavaScript")},
      {QStringLiteral("javascript"), QStringLiteral("JavaScript")},
      {QStringLiteral("ts"), QStringLiteral("TypeScript")},
      {QStringLiteral("typescript"), QStringLiteral("TypeScript")},
      {QStringLiteral("sh"), QStringLiteral("Bash")},
      {QStringLiteral("bash"), QStringLiteral("Bash")},
      {QStringLiteral("shell"), QStringLiteral("Bash")},
      {QStringLiteral("py"), QStringLiteral("Python")},
      {QStringLiteral("python"), QStringLiteral("Python")},
      {QStringLiteral("qml"), QStringLiteral("QML")},
  };
  return table;
}

}  // namespace

QString normalize(const QString& fenceInfoStringLanguage) {
  QString trimmed = fenceInfoStringLanguage.trimmed();
  const auto alias_entry = aliasTable().find(trimmed.toLower());
  if (alias_entry != aliasTable().end()) {
    return alias_entry.value();
  }
  return trimmed;
}

}  // namespace holonight_rendering::LanguageAlias
