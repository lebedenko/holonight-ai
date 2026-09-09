# UQC-104: Runtime Qt Quick Controls adoption

Status: Implemented and locally verified
Date: 2026-09-08

## Baselines and scope

- Umbrella: `a96ae0b0247d654e8da2c2a0e33780119b057c04`.
- AI: `b600674ce4c86d883d98a27ae783e056a6a2f0e6`.
- Provider: `50c59558bb3817f57a992dd72730dba141db1bc8`.
- Appearance configuration: `fe69a59e6b73167fd5349223a4d265d75386c139`.

All application standard controls, enums and attached properties use
`import QtQuick.Controls as Controls`. Core, composites, semantic painting,
composer bindings, provider drafts and credential masking retain their contracts.
Embed the overridable `:/qtquickcontrols2.conf` default (`Style=Holonight`).
No imperative selection, public configuration, database, credential, provider,
D-Bus or Wayland interface changes. Preserve the application/rendering metatype
merge and singleton registration pipeline and configure-time activation paths.

## Acceptance matrix

| Area | Required evidence |
|---|---|
| Policy | Runtime namespace enforcement, independently failing fixtures, existing composite adoption/selection and semantic checks |
| Application fixture | Production QML/assets with fake controllers/models and rendering registrations; no production network or credential singletons |
| Surfaces, both styles | Workspace, settings, four provider forms, empty/unsupported providers, background AI, ordinary-window QuickPanel |
| Behavior, both styles | Credential mask/reveal/clear, dirty navigation, composer bindings, model selection/overflow, scrolling, menus, tool/statistics popups |
| Origins | Resolved control implementation and loaded plugin paths; preserved Core/composites |
| Build/install launches | Embedded default, environment Fusion, command-line Fusion over environment Holonight, external Fusion configuration |
| Verification | Focused checks, full suite, QML suites in both styles, format, tidy, lint/types, staged install/activation prefix, script syntax, links, whitespace |

Production launches use temporary XDG config/data/cache, disposable SQLite,
explicit configuration disabling every provider and a private D-Bus daemon with
no service activation directories. No provider requests, credential operations or
tool executions. Fixtures must finish deterministically; production observation
is bounded and successful processes explicitly terminated and reaped. Early exits,
crashes, missing evidence, unexpected QML diagnostics and timeouts fail.
Live layer-shell, human-operated Hyprland/Sway and ecosystem activation belong to UQC-201.

## Supplemental acceptance

[UQC-203](UQC-203.md) preserves native composer control sizing across HoloNight
and Fusion and verifies polished row geometry against the integrated provider.
