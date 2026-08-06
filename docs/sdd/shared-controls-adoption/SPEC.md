# Shared Controls Adoption — Specification

Status: Implemented

Upstream baseline: `holonight-qt@7a74276`

## Goal

Adopt canonical `Holonight.Controls` components for clear semantic matches in the workspace,
settings, quick panel, and chat composition while preserving application behavior.

## Requirements

- Preserve the `HolonightChat` URI, packaging, controllers, models, public QML properties,
  signals, accessible names, and keyboard behavior.
- Use shared search, selection, action, empty-state, status, header, form, text-area, and
  separator contracts where they match.
- Retain application-owned conversation editing and delete confirmation, chat message surfaces,
  notices, provider icons, routing, popups, and responsive layout policy.
- Accept upstream sizing, spacing, focus, selection, and state visuals.
- Do not change C++ domain or provider APIs, upstream sources, secrets, lockfiles, or generated
  artifacts.
- Complete automated nonvisual verification before requesting manual visual approval.

## Acceptance

The focused QML construction and source-policy tests, QML lint, build, complete test suite,
format check, diff check, and offscreen scale-factor smoke checks pass. The user completed
`MANUAL_VISUAL_CHECKLIST.md` and approved the feature.
