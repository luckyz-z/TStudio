# Security

TStudio runs Lua scripts, Lua plugins, extensions and native DLL plugins. A native plugin or an unsandboxed script can do anything a normal program can, so only install plugins from people you trust.

## Reporting a problem

Please don't open a public issue for security problems. Use [private vulnerability reporting](https://github.com/Luckyz-z/TStudio/security/advisories/new) instead. Include steps to reproduce and the TStudio version.

Things we especially want to hear about:

- a project file (`.tsp`, `.flp`, `.mid`) that can run code or write files when opened
- the MCP server accepting connections from other machines
- a Lua plugin escaping the plugin API

## Supported versions

Only the latest release gets fixes.
