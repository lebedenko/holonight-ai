#pragma once

#include <QMetaType>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <cstdint>
#include <variant>

namespace holonight_rendering {

// Namespace-style holder purely so the enum gets a QML-visible name (`ContentBlockType.Code`)
// distinct from the `ContentBlock` value type itself. Deliberately capitalized despite Qt's
// "value type names should begin with a lowercase letter" startup warning: QML's grammar requires
// an uppercase-first identifier to resolve as a type/namespace for static member access
// (`ContentBlockType.Code`); a lowercase name resolves as a property/id lookup instead and fails
// silently, breaking every Loader that switches on this enum. Verified the hard way — renaming
// this to lowercase to silence the warning made all assistant message content blocks render empty.
class ContentBlockTypeNs {
  Q_GADGET
  QML_NAMED_ELEMENT(ContentBlockType)

 public:
  enum class Type : std::uint8_t { Markdown, Code };
  Q_ENUM(Type)
};

using ContentBlockType = ContentBlockTypeNs::Type;

// Discriminated union (REQ-F-001). Deliberately NOT a QObject: blocks are produced in batches of
// unpredictable size on every message completion, and QObject-per-block would mean parent/lifetime
// bookkeeping for a value that is conceptually immutable data. A Q_GADGET registered as a QML
// value type carries no such baggage and is copy/compare/assign like any other value.
class ContentBlock {
  Q_GADGET
  QML_VALUE_TYPE(contentBlock)

  Q_PROPERTY(holonight_rendering::ContentBlockType type READ type CONSTANT)
  Q_PROPERTY(QString id READ id CONSTANT)
  Q_PROPERTY(QString text READ text CONSTANT)          // Markdown prose, or the raw code — see text()
  Q_PROPERTY(QString language READ language CONSTANT)  // "" for Markdown blocks
  Q_PROPERTY(bool complete READ complete CONSTANT)

 public:
  ContentBlock() = default;  // required by QML_VALUE_TYPE; produces an empty Markdown block

  [[nodiscard]] static ContentBlock markdown(QString text, QString id = {}, bool complete = true);
  [[nodiscard]] static ContentBlock code(QString code, QString normalizedLanguage, QString id = {},
                                         bool complete = true);

  [[nodiscard]] ContentBlockType type() const noexcept { return type_; }
  [[nodiscard]] const QString& id() const noexcept { return id_; }
  // Markdown block: the raw markdown source slice (REQ-F-001, "retrievable as QString").
  // Code block: the raw code content (REQ-F-001, "retrievable as QString").
  [[nodiscard]] QString text() const;
  // Code block: the language after REQ-F-003 normalization. Markdown block: "".
  [[nodiscard]] QString language() const;
  [[nodiscard]] bool complete() const noexcept { return complete_; }

  [[nodiscard]] bool operator==(const ContentBlock&) const noexcept = default;

 private:
  struct MarkdownPayload {
    QString text;
    bool operator==(const MarkdownPayload&) const = default;
  };
  struct CodePayload {
    QString code;
    QString language;
    bool operator==(const CodePayload&) const = default;
  };

  ContentBlockType type_ = ContentBlockType::Markdown;
  QString id_;
  bool complete_ = true;
  std::variant<MarkdownPayload, CodePayload> payload_{MarkdownPayload{}};
};

}  // namespace holonight_rendering

Q_DECLARE_METATYPE(holonight_rendering::ContentBlock)
