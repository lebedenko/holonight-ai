# Provider Tool Runtime Policy

The runtime treats tool registration and provider availability as separate concerns. A registered
executor/presenter/renderer package is a provider-neutral application capability; it is not
automatically advertised to every provider.

For this cycle, only an Anthropic instance with `tool_calling_enabled` receives the local
`ToolCatalogSnapshot`. OpenAI, Google, and Ollama receive no local catalog, even when the registry
is populated. Their existing chat behavior is unchanged.

Adding tool support to another provider requires a provider-owned codec with focused contract
tests for all of these responsibilities:

- encode canonical definitions into the provider's native request shape;
- decode complete and streamed arguments into `ToolRequestEvent`;
- preserve native correlation IDs and classify local versus provider-hosted execution;
- encode local results for continuation; and
- reconstruct provider-native history from the lossless tool-call ledger.

That work must remain confined to provider translation and runtime selection. It must not require
changes to tool executors, presenters, transcript projection, or QML renderers. Availability,
execution permission, and disclosure remain independent future policy axes.
