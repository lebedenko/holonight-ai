# Shared Controls Adoption — Design

Status: Implemented

Upstream baseline: `holonight-qt@7a74276`

The executable links `HolonightQt::Controls`; QML continues to import the unversioned
`Holonight.Controls` module. Shared controls own their canonical visual and interaction contracts,
while application QML continues to own routing, bindings, commands, responsive composition, and
domain-specific surfaces.

Navigation and provider selection use semantic delegates. Search and model/provider choices use
shared inputs. Structural headers, action regions, and true dividers use shared composition
controls. Provider forms remain four separate panels and retain every controller binding.

The chat composer deliberately owns its editor instead of adopting `HnTextArea`. Its approved
single-surface composition needs the editor to share the composer's border, while also owning
Enter/Shift+Enter submission and bounded 3–8-line growth inside a scroll view. `HnTextArea` owns
its own frame and keyboard integration, so nesting it would create a second surface and prevent
the application from expressing that contract cleanly. The app-owned editor still uses the
HoloNight `TextArea` style and canonical palette tokens; this is a narrow exception rather than a
general replacement for shared inputs.

`ConversationListDelegate` remains custom because inline rename and destructive confirmation need
composition not exposed by the shared delegate API. Message cards, code rendering, notices,
provider icons, application popups, and decorative accents also remain local.

Automated verification is intentionally nonvisual. The user completed the final visual approval.
