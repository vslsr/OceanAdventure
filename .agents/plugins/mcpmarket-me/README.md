MCPmarket plugin for Codex CLI.

## Install

```
codex plugin marketplace add knoxgraeme/mcpmarket-plugin
```

Codex requires `[features] codex_hooks = true` in `~/.codex/config.toml`
for the `SessionStart` hook to fire.

## Configuration

Codex has no install-time prompt for credentials. Either:

- Download a pre-configured ZIP from https://app.mcpmarket.com (bakes
  credentials into `mcp-servers.toml` and `.mcp.json`), or
- Export them in your shell before launching Codex:

```
export MCPMARKET_TOKEN=sk_user_...
export MCPMARKET_TOOLKIT_URL=https://gateway.mcpmarket.com/<org>/toolkits/<toolkit>/mcp
```

## Layout

- `.codex-plugin/plugin.json` — plugin manifest
- `mcp-servers.toml` — Codex-native MCP config
- `.mcp.json` — same credentials in JSON for `shared/sync.sh`'s fallback
- `hooks/hooks.json` — `SessionStart` hook
- `hook-shim.sh` — exports `MCPMARKET_*` env vars then runs `shared/sync.sh`
