# Response Statistics Boundary — Specification

Status: Complete. This cycle is a behavior-preserving ownership refactor of the existing response
statistics UI.

## Functional requirements

### REQ-F-001: Dedicated statistics footer

`ResponseStatsFooter` shall own footer visibility, token and duration badges, the information
button, popup open/close behavior, and all usage bindings into `ResponseStatsPopup`.

### REQ-F-002: Explicit input contract

The component shall require assistant identity, message status, all six token-count values, and
duration. Undefined values shall remain undefined; `durationMs` shall remain the canonical usage
presence indicator.

### REQ-F-003: Behavioral compatibility

Statistics shall be visible only for assistant messages whose status is `complete` and whose
`durationMs` is defined. Optional token badges shall remain independently visible. Labels,
formatting, styling, accessibility text, object names, popup placement, and dismissal behavior
shall not change.

## Non-functional requirements

- **REQ-NF-001:** The extraction shall not alter C++ APIs, model roles, persistence, view-model
  behavior, or the `MessageBubble`/`ChatMessageDelegate` contracts.
- **REQ-NF-002:** Focused QML coverage shall exercise visibility, formatting, forwarding,
  accessibility, and popup toggling through stable seam names.
- **REQ-NF-003:** QML lint, formatting checks, and automated tests shall remain clean.

## Constraints

- **REQ-C-001:** No visual redesign or usage-role consolidation.
- **REQ-C-002:** Preserve `responseStatsFooter`, `responseStatsInfoButton`, and
  `responseStatsPopup`.
- **REQ-C-003:** Further decomposition of `ResponseStatsPopup` is out of scope.
