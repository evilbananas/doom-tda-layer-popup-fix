# LayerPopupFix

A `dwmapi.dll` proxy for DOOM: The Dark Ages that stops `idTechLauncher.exe` from asking "Would you like to disable these layers?" when ReShade is installed as a Vulkan layer.

## Why

The launcher lists the Vulkan layers on the system and shows the pop-up if `VK_LAYER_reshade` is one of them. Setting `rgl_validateLayers 0` doesn't help, because the launcher rewrites `launcherData\base\configs\launcher.cfg` on every start and ignores the launch option.

The Kaibz mod has its own option for this (`IsRemoveIdTechReshadePopUp`), but with ReShade 6.8.0.1 it sometimes crashed the game at startup. The crash was an access violation in `ReShade64.dll` on a thread started by the mod's `version.dll`. Turning that option off and using this DLL instead avoids the crash.

## How it works

- In `idTechLauncher.exe` only, it patches the launcher's import table entries for `GetProcAddress` and `vkGetInstanceProcAddr`.
- When the launcher looks up `vkEnumerateInstanceLayerProperties`, it gets a wrapper that drops any layer whose name starts with `VK_LAYER_reshade`.
- In every other process, including `DOOMTheDarkAges.exe` (which loads `dwmapi.dll` by name), it does nothing. ReShade still loads in the game.
- All 118 dwmapi exports, including the ordinal-only ones, are forwarded to `C:\Windows\System32\dwmapi.dll`.

The launcher imports `dwmapi.dll` and the game doesn't, which is why that DLL was picked. Kaibz already uses `version.dll`.

## Build

Needs Visual Studio with the C++ x64 tools. `build.cmd` points at Visual Studio 18 Community; change the `vcvars64.bat` path if yours is elsewhere.

```
build.cmd
```

This produces `dwmapi.dll` in this folder.

`dwmapi_exports.h` was generated from the system DLL's exports with `dumpbin /exports C:\Windows\System32\dwmapi.dll`. Regenerate it if a Windows update changes that list.

## Install

Copy `dwmapi.dll` next to `idTechLauncher.exe` in the game folder. To uninstall, delete it.

Vortex doesn't manage this file, so check it's still there after a purge or redeploy.

### If another mod already uses dwmapi.dll

Many mods load the same way, as a proxy for a Windows DLL dropped in the game folder (`version.dll`, `dinput8.dll`, `winmm.dll`, `dxgi.dll` and so on). Only one file can have each name, so check the game folder for an existing `dwmapi.dll` before copying this one in.

Don't rename this DLL to get around a clash. It only loads because `idTechLauncher.exe` imports `dwmapi.dll` by name, and it only forwards dwmapi's functions. Under another name it either never loads (`dinput8.dll` isn't imported by the launcher) or breaks whatever expects the real DLL (`version.dll` would be missing all of version.dll's functions).

If the name is taken, rebuild it as a proxy for a different DLL instead. It has to be one that the launcher imports, that the game exe doesn't import, and that isn't a Windows KnownDLL (those always load from System32). These fit:

- `wtsapi32.dll`
- `userenv.dll`
- `dwrite.dll`
- `authz.dll`
- `netapi32.dll`

To switch, regenerate `dwmapi_exports.h` from that DLL's exports in System32 (replace `dwmapi` with the new name in each forwarder), change the `/Fe:` output name in `build.cmd`, and rebuild. The hook code doesn't depend on the DLL name.

`version.dll` would also load in the launcher, but the Kaibz mod already uses that name.

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

- The forward target is the absolute path `C:\Windows\System32\dwmapi.dll`, so it assumes Windows is on `C:`.
- It only hides ReShade. Other layers the launcher objects to will still trigger the pop-up.
