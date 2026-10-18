# TStudio plugin exception

TStudio is licensed under the GNU Affero General Public License, version 3 (see [LICENSE](LICENSE)). The copyright holders grant this additional permission under section 7 of that license.

## Additional permission

You may create, distribute, sell and license **Plugins** under terms of your choice, including closed-source and commercial terms. A Plugin is not a work based on TStudio, and the AGPL does not apply to it, as long as it interacts with TStudio only through the **Plugin Interfaces**.

"Plugin Interfaces" means:

- the Lua plugin API described in `docs/LUA_PLUGINS.md`
- the Lua scripting and extension API described in `docs/LUA_API.md`
- the native plugin API in `SDK/tstudio_plugin.h`
- the MCP automation interface described in `docs/MCP.md`
- the VST3 plugin interface, for plugins TStudio hosts

"Plugins" include Lua plugins, scripts, extensions, native plugin DLLs, presets, sample packs, themes and MCP clients.

## What it doesn't cover

- Changes to TStudio itself, or code copied out of TStudio into your Plugin, stay under the AGPL.
- Distributing TStudio, including bundling it with a Plugin, follows the AGPL as usual.
- The bundled example plugins in `Plugins/`, `Extensions/` and `SDK/examples/` are AGPL. The same templates are available under the MIT license in the [TStudio-Plugins](https://github.com/Luckyz-z/TStudio-Plugins) repository, so you can start from them for any kind of plugin.

You may remove this exception from copies of TStudio you distribute. Contributions to TStudio are accepted under the AGPL with this exception.
