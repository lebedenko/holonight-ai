#pragma once

#include <QString>

namespace holonight_rendering::LanguageAlias {

// Pure function, no KSyntaxHighlighting dependency (REQ-F-003 is a string transform; deciding
// whether the *result* is a language KSyntaxHighlighting actually knows about is REQ-F-004's job,
// done later by CodeHighlighter).
//
// cpp/c++/cxx -> "C++"; js/javascript -> "JavaScript"; ts/typescript -> "TypeScript";
// sh/bash/shell -> "Bash"; py/python -> "Python"; qml -> "QML". Anything else (including "",
// "rust", "go") is returned unchanged (trimmed).
[[nodiscard]] QString normalize(const QString& fenceInfoStringLanguage);

}  // namespace holonight_rendering::LanguageAlias
