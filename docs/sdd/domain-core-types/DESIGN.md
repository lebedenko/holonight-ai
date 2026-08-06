# Domain Core Types Design

**Document Version**: 1.0
**Date**: 2026-07-21
**Module**: `holonight_domain` (core value types)
**Traces to**: `docs/sdd/domain-core-types/SPEC.md` (21 requirements, 5 deferred Implementation Notes)
**Status**: Implemented

---

## 1. Components

Four public headers under `src/domain/include/holonight_domain/`, one namespace (`holonight_domain`),
no `.cpp` files required for the value types themselves (see §6) except where `QUuid`/`QDateTime`
calls benefit from being out-of-line to limit `<QUuid>`/`<QDateTime>` include fan-out.

### 1.1 `MessageId` / `ConversationId`

Both follow an identical opaque-UUID pattern (REQ-F-001, REQ-F-008, REQ-C-001). Implemented as two
distinct classes (not a shared template) to keep the two independent per REQ-F-001/008's "distinct
type" wording and to give each a clean name in diagnostics/tooling; the bodies are intentionally
near-identical.

```cpp
class MessageId {
 public:
  static MessageId generate();

  [[nodiscard]] QString toString() const;

  friend bool operator==(const MessageId&, const MessageId&) = default;

 private:
  explicit MessageId(QString value);

  QString value_;
};
```

`ConversationId` is the same shape, substituting the class name. No public constructor taking a
`QString` exists — `generate()` and the private constructor are the only ways to produce an
instance, satisfying "prevents direct construction from arbitrary caller-supplied strings."
`operator==` is defaulted (member-wise), which also yields `operator!=` implicitly under C++20/23
rewritten-candidate rules.

Neither type overloads `operator<`; no ordering requirement exists in the spec, and adding one
would invite accidental lexicographic-UUID-ordering assumptions downstream. If `Conversation`
lookup by ID is later needed, that is an `application`/`persistence` concern (e.g. a `QHash` keyed
by `toString()`), not a `domain` concern.

### 1.2 `Message`

```cpp
enum class MessageRole { System, User, Assistant };

enum class MessageStatus { Pending, Streaming, Complete, Error, Cancelled };

struct TransitionError {
  MessageStatus from;
  MessageStatus attempted;
};

class Message {
 public:
  Message();
  Message(MessageId id, MessageRole role, QString text, MessageStatus status = MessageStatus::Pending);

  [[nodiscard]] const MessageId& id() const;
  [[nodiscard]] MessageRole role() const;
  [[nodiscard]] const QString& text() const;
  [[nodiscard]] MessageStatus status() const;

  std::expected<void, TransitionError> transitionTo(MessageStatus next);

  friend bool operator==(const Message&, const Message&) = default;

 private:
  MessageId id_;
  MessageRole role_ = MessageRole::User;
  QString text_;
  MessageStatus status_ = MessageStatus::Pending;
};
```

- Default constructor (REQ-F-004) exists to satisfy "implementation-level default constructor...
  `Pending` status and empty text"; it default-constructs `id_` via `MessageId`'s own default state.
  Since `MessageId` has no public default constructor (§1.1), `Message`'s default constructor calls
  `MessageId::generate()` internally rather than leaving `id_` in an unspecified state — a
  default-constructed `Message` is still a fully valid value with a real, unique id. This is called
  out as a design antipattern per the spec's own acceptance criterion, but "valid" is required, and
  a generated id is safer than any alternative (see §5).
- `transitionTo` is the sole mutator; there is no public `setStatus()` that bypasses validation.
- Equality (REQ-F-007) is defaulted, comparing all four fields in declaration order.
- No `attachments`, `tool_calls`, or `usage` members exist anywhere on the class (REQ-F-005); this
  is enforced by simply never declaring them, which is what "compile-time error" means for a
  closed, non-extensible value type — there is no extension point (no virtuals, no `std::any`
  member) through which a caller could add one without editing this header.

### 1.3 `Conversation`

```cpp
class Conversation {
 public:
  Conversation(ConversationId id, QString title, QDateTime createdAt);

  [[nodiscard]] const ConversationId& id() const;
  [[nodiscard]] const QString& title() const;
  [[nodiscard]] QDateTime createdAt() const;
  [[nodiscard]] QDateTime updatedAt() const;
  [[nodiscard]] const std::vector<Message>& messages() const;

  void appendMessage(Message message);

  friend bool operator==(const Conversation&, const Conversation&) = default;

 private:
  ConversationId id_;
  QString title_;
  QDateTime created_at_;
  QDateTime updated_at_;
  std::vector<Message> messages_;
};
```

- `updatedAt` defaults to `createdAt` at construction and is stamped to "now" only inside
  `appendMessage` (REQ-F-011); there is no public `setUpdatedAt()`, keeping the invariant
  "updatedAt changes iff a message was appended" enforced by the type itself rather than by
  caller discipline.
- `messages()` returns a `const&` to the vector rather than a copy or a custom range type — cheap,
  supports `.size()`/iteration/indexing directly (REQ-F-009, REQ-F-010) without inventing a new API
  surface.
- Defaulted `operator==` compares `id_`, `title_`, `created_at_`, `updated_at_`, `messages_` in
  that order, member-wise — `std::vector<Message>::operator==` is itself order-sensitive
  element-wise comparison, so REQ-F-012's "message order is significant" falls out for free.

### 1.4 `ModelId`

```cpp
struct ModelId {
  QString provider_id;
  QString model_name;

  friend bool operator==(const ModelId&, const ModelId&) = default;
};
```

Deliberately a `struct` with public data members, not a `class` with accessors. REQ-F-013 asks only
for "two fields... accessible via named accessors" and equality; there is no invariant to protect
(no validated format, no factory) the way `MessageId`/`Message` have. A `struct` keeps it a true
POD-like value type consistent with `ModelId` being explicitly capability-free ("no fields for
context window..."), and avoids writing four lines of accessor boilerplate for two plain strings.
Accessors here would be `provider_id`/`model_name` read directly rather than via camelBack methods;
see §4 for why this is an intentional deviation, guarded so it cannot silently regress into a
richer type.

Equality (REQ-F-014) is defaulted, member-wise `QString::operator==`, which is case-sensitive —
documented as the chosen (and only) comparison semantics; callers wanting case-insensitive
provider matching normalize before constructing a `ModelId`.

### 1.5 `StreamEvent`

```cpp
struct ContentDelta {
  QString text;
  friend bool operator==(const ContentDelta&, const ContentDelta&) = default;
};

struct Completed {
  friend bool operator==(const Completed&, const Completed&) = default;
};

struct Error {
  QString message;
  friend bool operator==(const Error&, const Error&) = default;
};

struct Cancelled {
  friend bool operator==(const Cancelled&, const Cancelled&) = default;
};

using StreamEvent = std::variant<ContentDelta, Completed, Error, Cancelled>;
```

- Confirmed: `std::variant<ContentDelta, Completed, Error, Cancelled>` (REQ-F-015) — a closed sum
  type by construction. `std::variant` cannot be constructed from, or converted to, any type
  outside its alternative list, so "constructing with a variant type outside these four does not
  compile" (REQ-F-015's acceptance criterion) is satisfied by the language itself, no extra
  guard code needed.
- `StreamEvent` is a type alias, not a wrapper class, so `std::holds_alternative<T>`,
  `std::get<T>`/`std::get_if<T>`, and `std::visit` all work directly and idiomatically — this is
  the "method or pattern... to safely query which variant is active" the spec asks for
  (REQ-F-015), reusing standard library vocabulary instead of hand-rolling one.
- Equality (REQ-F-021): `std::variant`'s own `operator==` already implements exactly the required
  semantics — two variants compare equal iff `index()` matches *and* the held alternatives compare
  equal — so no custom `operator==` is written for `StreamEvent` at all. Each alternative struct
  supplies its own defaulted `operator==` (`ContentDelta`/`Error` compare their one field;
  `Completed`/`Cancelled`, having no fields, always compare equal to their own type), and
  `std::variant` composes those automatically.
- Visitation for consumers (e.g. the future `application`/`providers` layers turning a
  `StreamEvent` into a `Message` mutation) uses `std::visit` with an overload-set helper
  (`Overloaded` pattern) — not part of this module's public API, just a note that the chosen
  representation supports it with zero adaptation.
- REQ-F-020 (no `ToolCall`/`Usage`) is satisfied by omission: the variant's alternative list is
  exactly four types, and adding a fifth is a one-line, obviously-reviewable header edit, not a
  runtime-configurable registration mechanism.

---

## 2. Data Flow

`holonight_domain` types are pure values with no I/O and no knowledge of their consumers; this
section exists only to justify the shapes chosen above, not to design those consumers.

- **`application`** will own a `Conversation` per open chat, calling `appendMessage()` when the
  user submits text and again (or mutating the trailing `Message`'s status via `transitionTo`) as
  a provider stream progresses. This is why `appendMessage` — not direct vector access — is the
  only mutation path: `application` needs the `updatedAt` side effect to be automatic and
  unforgeable.
- **`providers`** will produce a sequence of `StreamEvent` values off the wire (SSE/streaming HTTP
  chunks, translated per-provider) and feed them to `application`, which folds `ContentDelta` into
  the in-progress `Message.text`, and folds `Completed`/`Error`/`Cancelled` into a `transitionTo`
  call. This is why `StreamEvent` is a flat closed variant rather than a class hierarchy: providers
  emit one discrete event at a time, and a switch-like consumer (`std::visit`) is the natural
  shape, with the compiler enforcing exhaustiveness once a 5th variant is ever added.
- **`persistence`** will serialize `Conversation`/`Message` (and, eventually, `ModelId` once a
  conversation records which model produced it — out of scope this cycle) to SQLite rows. This is
  why field access is via plain accessors returning value types (`QString`, `QDateTime`,
  `std::vector<Message>`) rather than opaque handles — a persistence repository needs to read every
  field to build an `INSERT`/`UPDATE`, with no serialization framework in `domain` itself
  (REQ-NF-004).

None of the above is implemented in this cycle; it only constrains today's API shape so that no
foreseeable rework is forced on `application`/`providers`/`persistence` later.

---

## 3. Interfaces / APIs

All four headers live at `src/domain/include/holonight_domain/` and open with
`namespace holonight_domain {` / close with `}`. Include guards use `#pragma once` (consistent with
the current placeholder header). No header includes another domain header transitively in a cycle;
`conversation.h` includes `message.h` (for `std::vector<Message>`), and nothing else cross-includes.

### `message.h`

```cpp
#pragma once

#include <expected>

#include <QString>

namespace holonight_domain {

enum class MessageRole { System, User, Assistant };
enum class MessageStatus { Pending, Streaming, Complete, Error, Cancelled };

struct TransitionError {
  MessageStatus from;
  MessageStatus attempted;
};

class MessageId {
 public:
  static MessageId generate();
  [[nodiscard]] QString toString() const;
  friend bool operator==(const MessageId&, const MessageId&) = default;

 private:
  explicit MessageId(QString value);
  QString value_;
};

class Message {
 public:
  Message();
  Message(MessageId id, MessageRole role, QString text, MessageStatus status = MessageStatus::Pending);

  [[nodiscard]] const MessageId& id() const;
  [[nodiscard]] MessageRole role() const;
  [[nodiscard]] const QString& text() const;
  [[nodiscard]] MessageStatus status() const;

  std::expected<void, TransitionError> transitionTo(MessageStatus next);

  friend bool operator==(const Message&, const Message&) = default;

 private:
  MessageId id_;
  MessageRole role_ = MessageRole::User;
  QString text_;
  MessageStatus status_ = MessageStatus::Pending;
};

}  // namespace holonight_domain
```

### `conversation.h`

```cpp
#pragma once

#include <vector>

#include <QDateTime>
#include <QString>

#include "holonight_domain/message.h"

namespace holonight_domain {

class ConversationId {
 public:
  static ConversationId generate();
  [[nodiscard]] QString toString() const;
  friend bool operator==(const ConversationId&, const ConversationId&) = default;

 private:
  explicit ConversationId(QString value);
  QString value_;
};

class Conversation {
 public:
  Conversation(ConversationId id, QString title, QDateTime createdAt);

  [[nodiscard]] const ConversationId& id() const;
  [[nodiscard]] const QString& title() const;
  [[nodiscard]] QDateTime createdAt() const;
  [[nodiscard]] QDateTime updatedAt() const;
  [[nodiscard]] const std::vector<Message>& messages() const;

  void appendMessage(Message message);

  friend bool operator==(const Conversation&, const Conversation&) = default;

 private:
  ConversationId id_;
  QString title_;
  QDateTime created_at_;
  QDateTime updated_at_;
  std::vector<Message> messages_;
};

}  // namespace holonight_domain
```

### `model_id.h`

```cpp
#pragma once

#include <QString>

namespace holonight_domain {

struct ModelId {
  QString provider_id;
  QString model_name;

  friend bool operator==(const ModelId&, const ModelId&) = default;
};

}  // namespace holonight_domain
```

### `stream_event.h`

```cpp
#pragma once

#include <variant>

#include <QString>

namespace holonight_domain {

struct ContentDelta {
  QString text;
  friend bool operator==(const ContentDelta&, const ContentDelta&) = default;
};

struct Completed {
  friend bool operator==(const Completed&, const Completed&) = default;
};

struct Error {
  QString message;
  friend bool operator==(const Error&, const Error&) = default;
};

struct Cancelled {
  friend bool operator==(const Cancelled&, const Cancelled&) = default;
};

using StreamEvent = std::variant<ContentDelta, Completed, Error, Cancelled>;

}  // namespace holonight_domain
```

### `holonight_domain.h`

Repurposed (not removed) as the umbrella header REQ-NF-002 asks for ("a root header... exposes all
four to downstream consumers"). It becomes a pure re-export with no new declarations of its own —
the `application` module and `apps/chat` should be able to write a single
`#include <holonight_domain/holonight_domain.h>` and get everything:

```cpp
#pragma once

#include "holonight_domain/conversation.h"
#include "holonight_domain/message.h"
#include "holonight_domain/model_id.h"
#include "holonight_domain/stream_event.h"
```

Its current placeholder comment ("Conversation, message, model, and attachment domain types will
live here") is deleted; `attachment` is dropped from the comment since attachments are an explicit
Non-Goal (SPEC.md §Non-Goals) and no `attachment.h` exists in this cycle.

---

## 4. Key Decisions With Rationale

### 4.1 Status-transition rejection mechanism → `std::expected<void, TransitionError>`

`Message::transitionTo` returns `std::expected<void, TransitionError>`. `<expected>` is a
standard C++23 header (REQ-NF-001 already commits the module to C++23), so this introduces no new
dependency — unlike an exception type, it needs no `holonight_domain`-specific hierarchy, and
unlike `std::optional<Error>` it lets the success path carry `void` while still naming the failure
payload's type distinctly from "no value," which reads more clearly at call sites
(`if (auto r = msg.transitionTo(...); !r) { report(r.error()); }`).  It also composes with the
project's general C++23 lean (`CLAUDE.md`'s C++23 mandate, `.clang-tidy`'s `modernize-*` checks)
better than throwing, per REQ-C-002's implicit spirit of predictable, allocation-free control flow —
exceptions have non-trivial unwind cost and are awkward to test exhaustively with GTest's
`EXPECT_*` macros compared to a returned value that's directly assertable.

### 4.2 `StreamEvent` representation → `std::variant<ContentDelta, Completed, Error, Cancelled>`

Confirmed as specified in the Implementation Notes' leaning. `std::variant` is standard-library,
adds zero dependencies (REQ-NF-003/004), gives closed-set construction for free at the language
level (REQ-F-015, REQ-F-020), and its defaulted `operator==` composition (index + payload) maps
exactly onto REQ-F-021 with no hand-written comparison logic — the fewer lines of hand-written
comparison code, the fewer places a bug can hide.

### 4.3 String type → `QString` throughout the public API

`QString` is used for every string-bearing field/parameter (`Message::text`, `ModelId::provider_id`
/`model_name`, `Error::message`, `ContentDelta::text`, id types' internal storage). Rationale:

- REQ-NF-003 already forces a dependency on `Qt6::Core` for `QUuid` and `QDateTime`; once that
  dependency exists, introducing a second string type (`std::string`) alongside `QString` buys
  nothing and costs a conversion (`QString::fromStdString`/`toStdString`) at every boundary between
  domain types and, eventually, QML (per `CLAUDE.md`'s "Future: QML Singletons" note — these types
  or their DTOs will likely be exposed to QML/`Q_PROPERTY` in a later cycle, and `QString` is the
  zero-friction type there: `Q_PROPERTY(QString text ...)` needs no custom conversion, whereas
  `std::string` would need one at every property boundary).
- A single string type end-to-end (public API and private storage both `QString`) avoids the
  "mixed" option's foot-gun of picking wrong at each call site and avoids maintaining two parallel
  string-handling mental models in one small module.

### 4.4 `Error` variant shape → message only (`QString message`), no error code

`StreamEvent::Error` carries exactly one field: `message` (`QString`). No numeric/enum error code,
no platform-specific code (`errno`/`HRESULT`) is included in this cycle. Rationale: REQ-F-018 asks
for "at minimum an error message... additional fields... are design-stage decisions," and no
provider adapter exists yet (providers are explicitly out of scope, SPEC.md Non-Goals) to define
what a meaningful, cross-provider error taxonomy would even look like — inventing a code enum now
risks modeling the wrong thing and having to break it once `holonight_providers` lands. A single
free-text `message` is forward-compatible: a future cycle can add a `code` field to the `Error`
struct without touching `StreamEvent`'s variant list or any exhaustive `std::visit` call site (only
struct-literal constructors would need updating, and those don't exist outside tests yet).

### 4.5 `Conversation` message storage → `std::vector<Message>`

Confirmed. `std::vector` gives O(1) amortized append (the only mutation `Conversation` exposes),
contiguous storage (cheapest for the `persistence` layer's later "read every message and write
rows" access pattern, and for defaulted `operator==` which is itself sequential element compare),
and is the only one of the three candidates the spec itself calls out as satisfying order
preservation "e.g. `std::vector`, not an unordered map or set" (REQ-F-010's acceptance criteria).
`std::deque`/`std::list` offer no advantage here since `Conversation` never inserts/removes from
the middle or front, only appends at the back — `std::vector::push_back` is strictly sufficient
and is also the least surprising to profile under REQ-C-002 (no extraneous heap-management beyond
the vector's own reallocation-doubling policy).

---

## 5. Alternatives Considered

- **Transition mechanism — exceptions**: rejected. Throwing on every invalid transition attempt
  makes the "expected, recoverable" case (a provider double-firing a `Cancelled` event, or a race
  between user-cancel and provider-complete) exception-driven control flow, which is both a
  performance concern and awkward to assert against in GTest without wrapping every negative test
  in `EXPECT_THROW`. `std::expected` keeps the same information available without unwinding.
- **Transition mechanism — `std::optional<Error>`/bool + assert**: rejected. A bare
  `bool`/`optional` loses *why* a transition was rejected (which `TransitionError::attempted`
  captures), and "assertions and UB in debug builds" was explicitly one of the spec's floated
  options but fails REQ-F-006's "NOT silently accepted" in release builds where `assert` compiles
  out — an `std::expected` return is checked (or ignored) uniformly in every build configuration.
- **`StreamEvent` — class hierarchy with virtual `visit()`**: rejected. A base
  `class StreamEvent { virtual ... };` with four derived classes requires heap allocation
  (`std::unique_ptr<StreamEvent>`) to store polymorphically, which directly conflicts with
  REQ-C-002 ("no additional smart pointers... beyond what is implicit in `QString`/`std::string`/
  `std::vector<Message>`"). `std::variant` stores all four alternatives inline, with no additional
  allocation.
- **`StreamEvent` — hand-rolled tagged union (enum + union)**: rejected. Manually managing an
  active-member tag alongside a C-style `union` (or placement-new'd storage) reimplements exactly
  what `std::variant` already provides safely, and this codebase's `.clang-tidy` config enables
  `modernize-*` and `cppcoreguidelines-*`, both of which push toward standard-library facilities
  over hand-rolled equivalents.
- **String type — `std::string` throughout**: rejected per §4.3 (forces conversions at the
  `Qt6::Core`/future-QML boundary with no compensating benefit, given `Qt6::Core` is already a hard
  dependency).
- **String type — mixed (`QString` public, `std::string` private)**: rejected. Doubles the string
  types the module must reason about for zero behavioral gain — private storage never needs to be
  a different type than the public accessor's return type when there's no perf-critical
  transformation happening between them (there isn't one here).
- **`ModelId` — full class with private members + accessors (`providerId()`/`modelName()`)**:
  considered, not chosen. Rejected in favor of a plain `struct` with public members (§1.4) because
  `ModelId` has no invariant to protect and REQ-F-013 explicitly bounds it to "no capability
  metadata" ever — the simplest type that satisfies "accessible via named accessors" without adding
  boilerplate is a struct whose members *are* the accessors (`model.provider_id`). If a future
  requirement adds validation (e.g. non-empty provider), converting to accessor methods at that
  point is a mechanical, localized change.
- **Conversation storage — `std::deque<Message>`**: rejected; no front-insertion or mid-sequence
  insertion requirement exists, so `deque`'s non-contiguous-storage cost buys nothing over
  `vector`'s cheaper iteration/copy for `persistence`'s eventual full-scan-and-serialize use.
- **Conversation storage — `std::list<Message>`**: rejected; O(1) insertion anywhere is not a
  requirement (only back-append is used), and `list`'s per-node allocation directly works against
  REQ-C-002's spirit of minimal, predictable heap use — `vector` amortizes to one contiguous
  allocation instead of one allocation per `Message`.

---

## 6. `CMakeLists.txt` Changes

`src/domain/CMakeLists.txt` changes from an `INTERFACE` library to a `STATIC` library. Concrete
diff-level description:

```diff
-# Empty for now — switch to add_library(holonight_domain STATIC ...) once the
-# first .cpp file lands here.
-add_library(holonight_domain INTERFACE)
+add_library(holonight_domain STATIC
+    src/message.cpp
+    src/conversation.cpp
+    src/model_id.cpp
+    src/stream_event.cpp
+)

 target_include_directories(holonight_domain
-    INTERFACE
+    PUBLIC
     ${CMAKE_CURRENT_SOURCE_DIR}/include
 )

 target_link_libraries(holonight_domain
-    INTERFACE
+    PUBLIC
     Qt6::Core
 )

-target_compile_features(holonight_domain INTERFACE cxx_std_23)
+target_compile_features(holonight_domain PUBLIC cxx_std_23)
```

Notes:

- `Qt6::Core` stays `PUBLIC` (not `PRIVATE`): every consumer's translation units that include
  `holonight_domain` headers directly use `QString`/`QDateTime` types by value in signatures, so
  they need `Qt6::Core`'s include paths/definitions transitively — this also matches REQ-NF-003's
  requirement that the target lists only `Qt6::Core`.
- `target_include_directories` stays `PUBLIC` for the same reason: `apps/chat`, `holonight_application`,
  etc. need `#include <holonight_domain/...>` to resolve.
- Source file list: `src/message.cpp`, `src/conversation.cpp`, `src/model_id.cpp`,
  `src/stream_event.cpp` under `src/domain/src/` (new directory, per SPEC.md's Module Layout
  section: "Implementation (under `src/domain/src/`)"). `model_id.cpp` and `stream_event.cpp` may
  end up empty-or-near-empty translation units that exist only to give the static library at least
  one non-header-only object file per type family for symmetry/discoverability; if a given header
  truly has no out-of-line definitions (e.g. `stream_event.h` is fully inline — aliases and
  defaulted-comparison structs need no `.cpp`), it is acceptable to omit that `.cpp` file entirely
  and list only the headers that need one (`message.cpp` for `MessageId`/`Message`'s out-of-line
  constructors and `transitionTo`; `conversation.cpp` for `ConversationId`/`Conversation`'s
  out-of-line constructors, `appendMessage`). This is a Development-phase call once the exact
  inlining boundary is drawn; either way, `add_library(... STATIC ...)` must list every `.cpp` file
  actually created, and CMake requires at least one source file for a non-empty static library —
  if every type ends up header-only-inline, an `add_library(holonight_domain STATIC)` with zero
  sources is invalid, so at minimum a small `src/domain.cpp` (or the per-type files above) must
  exist.
- No other targets change in this cycle — `holonight_application`'s existing
  `target_link_libraries(holonight_application INTERFACE holonight_domain Qt6::Core)` continues to
  work unmodified since `holonight_domain` remains a linkable target either way.

---

## 7. Test File Organization

New directory `tests/domain/`, one file per header/type family, mirroring `src/domain/include/holonight_domain/`'s
four-header split so a reader can find the test for a header by name alone:

```
tests/
├── CMakeLists.txt          (modified)
├── main.cpp                (unchanged)
├── test_placeholder.cpp    (unchanged, or removed once real coverage lands — Development-phase call)
└── domain/
    ├── test_message.cpp        # MessageId, Message: construction, accessors, default status,
    │                           # equality, all valid/invalid transitions (REQ-F-001..007)
    ├── test_conversation.cpp   # ConversationId, Conversation: construction, accessors,
    │                           # append/order preservation, updatedAt sync, equality (REQ-F-008..012)
    ├── test_model_id.cpp       # ModelId: construction, equality (REQ-F-013..014)
    └── test_stream_event.cpp   # StreamEvent: variant construction/holds_alternative per
                                # variant, equality within and across variants (REQ-F-015..021)
```

`tests/CMakeLists.txt` changes:

```diff
 add_executable(test_holonight_ai
   main.cpp
   test_placeholder.cpp
+  domain/test_message.cpp
+  domain/test_conversation.cpp
+  domain/test_model_id.cpp
+  domain/test_stream_event.cpp
 )
 set_target_properties(test_holonight_ai PROPERTIES AUTOMOC ON)
 target_compile_features(test_holonight_ai PRIVATE cxx_std_23)
 target_link_libraries(test_holonight_ai PRIVATE
   GTest::gtest
   GTest::gmock
   Qt6::Gui
+  holonight_domain
 )
```

`holonight_domain` must be added to `target_link_libraries` — the existing placeholder test only
needs `Qt6::Gui`/GTest and never linked any `src/*` module. `Qt6::Gui` stays (needed elsewhere in
the harness's `QT_QPA_PLATFORM=offscreen` setup for GUI-adjacent tests); it is not needed by domain
tests themselves, which pull in `Qt6::Core` transitively via `holonight_domain`.

Discovery/execution is unchanged: `gtest_discover_tests(test_holonight_ai DISCOVERY_MODE PRE_TEST ...)`
already globs all `TEST(...)` cases linked into the one `test_holonight_ai` binary, so new files
need no additional CTest registration beyond being added to `add_executable`'s source list.
`test_placeholder.cpp` can stay indefinitely (harmless) or be deleted once domain tests land and
prove the toolchain independently — a Development-phase housekeeping call, not a Design blocker.

Each new test file uses plain `TEST(SuiteName, CaseName)` GTest bodies (no fixtures needed — none
of these types require setup/teardown state), e.g. `TEST(Message, TransitionPendingToStreamingSucceeds)`,
`TEST(Conversation, AppendMessageUpdatesUpdatedAt)`, following the existing
`TEST(Placeholder, AlwaysPasses)` naming style already present in `test_placeholder.cpp`.

---

## 8. Known Risks

- **`std::expected` toolchain maturity**: low risk for this project specifically. `std::expected`
  (P0323) shipped in libstdc++ starting GCC 12 and in libc++ starting LLVM 16; this environment
  has GCC 16.1.1 and Clang 22.1.8 available, both comfortably past those baselines, so no fallback
  is designed. If the project's CI or a contributor's toolchain is ever pinned to something older,
  `<expected>` would fail to resolve at configure/compile time with a clear "no such header" error
  (not a silent miscompile) — the mitigation, should it ever be needed, is bumping the minimum
  compiler version documented in `docs/dev-setup.md`, not redesigning `transitionTo`'s signature.
- **`QDateTime` resolution vs. REQ-F-011's "strictly later" test**: real risk, flagged explicitly.
  `QDateTime::currentDateTime()`/`currentDateTimeUtc()` on Linux/Qt6 report millisecond resolution,
  which is normally sufficient, but a test that calls `appendMessage` twice back-to-back with no
  delay (or under heavy CI load with clock coalescing) could observe T1 == T2 at millisecond
  granularity and flake. Mitigation (Development-phase, to apply in `test_conversation.cpp`):
  the acceptance criteria in SPEC.md already anticipate this ("wait a measurable time interval,
  e.g. at least 1 millisecond") — the test for REQ-F-011 must include an explicit sleep
  (`QThread::sleep`/`std::this_thread::sleep_for(std::chrono::milliseconds(2))`, 2ms rather than 1
  to comfortably clear rounding) between capturing T1 and calling `appendMessage`, rather than
  relying on wall-clock scheduling jitter alone to separate the two timestamps. `Conversation`
  itself should use `QDateTime::currentDateTimeUtc()` (not local time) for `updatedAt` stamping —
  UTC avoids any DST-transition edge case affecting comparison, and is the more natural choice for
  eventual `persistence` serialization to SQLite (REQ-NF-003 already scopes `QDateTime` as the
  chosen timestamp type; UTC is a refinement within that choice, not a new dependency).
- **Defaulted `operator==` on `QString`-bearing types under `-Wall`/clang-tidy**: `= default`
  comparison operators require every member to itself be equality-comparable; `QString`,
  `QDateTime`, `std::vector<Message>`, and `std::variant<...>` all are, so this is expected to be
  friction-free, but worth flagging as the first thing to check if `task tidy`/`task build` surface
  an unexpectedly opaque template error during Development — the fix is almost always a missing
  `#include` (e.g. `<QString>` not pulled in transitively where expected) rather than a logic bug.
- **`ModelId` as a public-member struct**: a plain struct with public `provider_id`/`model_name`
  members deviates from `.clang-tidy`'s general expectation of accessor-based encapsulation
  (`MemberCase: lower_case`, `PrivateMemberSuffix: _` implies the convention assumes private
  members are the norm). This is a deliberate, narrow exception (§1.4, §5) for a two-field
  capability-free value type; if `clang-tidy`'s `cppcoreguidelines-*` checks ever flag public data
  members here, the mitigation is a targeted `// NOLINT` with a comment pointing back to this
  design doc's rationale, not blanket-disabling the check.
- **`Message`'s default constructor calling `MessageId::generate()`**: slightly surprising — a
  "default" constructor that isn't cheap/trivial (it invokes `QUuid::createUuid()`) — flagged in
  §1.2 as the spec's own acknowledged antipattern. Risk is mostly stylistic/performance-adjacent
  (default-constructing a `std::vector<Message>` of size N to later overwrite would generate N
  UUIDs pointlessly); if `application`/`persistence` code is ever seen default-constructing
  `Message` in a loop before assigning real values, that call site should switch to the
  parameterized constructor instead — not a reason to redesign the default constructor itself,
  since REQ-F-004 requires it to remain "valid."

---

## Document History

| Version | Date       | Author | Changes                                    |
|---------|------------|--------|---------------------------------------------|
| 1.0     | 2026-07-21 | Claude | Initial design from SPEC.md v1.0            |
