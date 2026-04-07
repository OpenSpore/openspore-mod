# OpenSpore Mod

Multiplayer mod for Spore's Space Stage using [SporeModAPI](https://github.com/emd4600/Spore-ModAPI). Connects to the [OpenSpore Server](https://github.com/OpenSpore/openspore-server) for asynchronous shared-galaxy multiplayer.

## Features

- **Empire Registration** - automatically registers your empire with the server when entering Space Stage
- **Heartbeat** - keeps your empire online status updated (30s interval)
- **Galaxy Sync** - polls other players' empires and displays them in the galaxy view
- **SSE Events** - real-time notifications for new empires and diplomacy actions
- **Diplomacy** - alliance, war, and trade offers synced with the server

## Requirements

- [Spore ModAPI Development Kit](https://github.com/emd4600/Spore-ModAPI/releases) (v2.5.x+)
- Visual Studio 2017+ or CMake 3.16+
- Windows 10 SDK
- Spore (Steam/Origin/Disk with Galactic Adventures)

## Build

### CMake

```bash
mkdir build && cd build
cmake .. -DSPORE_MODAPI_PATH="C:/path/to/Spore ModAPI" -DSPORE_VERSION=steam_patched
cmake --build . --config Release
```

`SPORE_VERSION` can be `disk`, `steam`, or `steam_patched`.

### Visual Studio

1. Install the SporeModAPI VS templates
2. Create a new SporeModAPI project and copy the `src/` files into it
3. Add nlohmann/json to your include paths
4. Link `winhttp.lib` and `shlwapi.lib`
5. Build for your Spore version

## Install

1. Copy `OpenSporeMod.dll` to your Spore ModAPI mods directory
2. Launch Spore via the ModAPI Launcher Kit
3. Enter Space Stage - the mod connects automatically

## Configuration

Config is saved to `%APPDATA%/OpenSpore/config.json`:

```json
{
  "playerId": "auto-generated",
  "empireId": "assigned by server",
  "authToken": "assigned by server",
  "serverHost": "localhost",
  "serverPort": 8080
}
```

Edit `serverHost` and `serverPort` to point to your OpenSpore server instance.

## Architecture

```
src/
  dllmain.cpp          Entry point (ModAPI DLL lifecycle)
  OpenSporeMod.h/cpp   Main mod class (IUpdatable + message listener)
  net/
    HttpClient.h/cpp   Async WinHTTP wrapper (background threads)
    ApiClient.h/cpp    Typed API client for all server endpoints
    SseClient.h/cpp    Server-Sent Events stream client
```

All HTTP requests run on background threads. Callbacks are queued and dispatched on the game thread via `Update()` to avoid race conditions with Spore's internals.

## License

GPL-3.0
