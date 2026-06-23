# CrossServerTeleport

A LeviLamina native plugin that provides cross-server teleport menus for Bedrock dedicated servers.

## Features

- `/servertp` command with `/stp` alias.
- Player-facing server selection and teleport confirmation forms.
- Operator-only management forms for adding, editing, deleting, enabling, disabling, and reloading servers.
- Configurable teleport delay, global enable switch, and optional permission flags.
- Transfer is performed with Bedrock `TransferPacket`.

## Config

The plugin creates its config at:

```text
plugins/cross-server-teleport/config/config.json
```

Server addresses use `host:port`. If the port is omitted or invalid, `19132` is used.

## Build

```powershell
xmake f -m release
xmake build
```

The packed plugin is generated under `bin/cross-server-teleport`.

