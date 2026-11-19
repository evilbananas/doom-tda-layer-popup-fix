# LayerPopupFix

A proxy DLL for DOOM: The Dark Ages that stops `idTechLauncher.exe` from asking "Would you like to disable these layers?" when ReShade is installed as a Vulkan layer.

## Why

The launcher lists the Vulkan layers on the system and shows the pop-up if `VK_LAYER_reshade` is one of them. Setting `rgl_validateLayers 0` doesn't help, because the launcher rewrites `launcherData\base\configs\launcher.cfg` on every start and ignores the launch option.

The Kaibz mod has its own option for this (`IsRemoveIdTechReshadePopUp`), but with ReShade 6.8.0.1 it sometimes crashed the game at startup. The crash was an access violation in `ReShade64.dll` on a thread started by the mod's `version.dll`. Turning that option off and using this DLL instead avoids the crash.

## How it works

- In `idTechLauncher.exe` only, it patches the launcher's import table entries for `GetProcAddress` and `vkGetInstanceProcAddr`.
- When the launcher looks up `vkEnumerateInstanceLayerProperties`, it gets a wrapper that drops any layer whose name starts with `VK_LAYER_reshade`.
- In every other process, including `DOOMTheDarkAges.exe`, it does nothing. ReShade still loads in the game.
- Every export of the real DLL, including ordinal-only ones, is forwarded to the copy in `System32`.

## Which DLL to use

The same code is built under five names. Each one is a DLL that the launcher imports and that isn't a Windows KnownDLL (those always load from System32). The game process also loads some of them at runtime, which is harmless because the proxy only forwards calls outside the launcher.

| File | Notes |
|---|---|
| `dwmapi.dll` | Default |
| `userenv.dll` | Alternative |
| `dwrite.dll` | Alternative |
| `authz.dll` | Alternative |
| `netapi32.dll` | Alternative |

All five were tested in a full launch: no pop-up, ReShade loaded in the game, clean exit.

`wtsapi32.dll` also fits on paper but isn't offered. As a proxy it froze the game at startup. The launcher's crash-dump code calls into wtsapi32, and a dump it started never finished.

Use `dwmapi.dll` unless another mod already puts a `dwmapi.dll` in the game folder. Many mods load as a proxy for a Windows DLL (`version.dll`, `dinput8.dll`, `winmm.dll`, `dxgi.dll` and so on), and only one file can have each name. If `dwmapi.dll` is taken, pick the first name from the table that isn't.

Install exactly one. Two of them would both hook the launcher; that still works, but only the first one gets to write the log.

Don't rename a file to get around a clash. Each one only forwards the functions of the DLL it's named after, so renamed it either never loads (the launcher doesn't import `dinput8.dll`) or breaks whatever expects the real DLL. `version.dll` isn't offered because the Kaibz mod already uses it.

## Install

Copy the one DLL next to `idTechLauncher.exe` in the game folder. To uninstall, delete it.

Vortex doesn't manage this file, so check it's still there after a purge or redeploy.

## Build

Needs Visual Studio with the C++ x64 tools. `build.cmd` points at Visual Studio 18 Community; change the `vcvars64.bat` path if yours is elsewhere.

```
build.cmd              builds all five into out\
build.cmd dwmapi       builds out\dwmapi.dll only
```

For each name, `gen_exports.ps1` reads the exports of `%SystemRoot%\System32\<name>.dll` with `dumpbin` and writes the forwarder list to `build\<name>\proxy_exports.h`, which `build.cmd` force-includes into `LayerPopupFix.cpp`. Rebuild if a Windows update changes a DLL's exports.

## Log

Each launcher start rewrites `LayerPopupFix.log` in the game folder. A working run looks like this:

```
16:23:50.741  LayerPopupFix loaded into idTechLauncher.exe
16:23:50.741  vkGetInstanceProcAddr import: hooked
16:23:50.741  GetProcAddress imports hooked: 1
16:23:50.839  launcher looked up vkEnumerateInstanceLayerProperties via GetProcAddress; wrapped it
16:23:50.843  hid layer VK_LAYER_reshade from the launcher
```

## Limits

- Forwarders point at the absolute path of the build machine's `%SystemRoot%\System32` (`C:\Windows\System32` for the release builds), so they assume Windows is installed there.
- It only hides ReShade. Other layers the launcher objects to will still trigger the pop-up.
