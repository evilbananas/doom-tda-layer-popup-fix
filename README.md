# LayerPopupFix

A proxy DLL for DOOM: The Dark Ages that stops `idTechLauncher.exe` from showing the "Would you like to disable these layers?" pop-up when ReShade is installed as a Vulkan layer.

Inside the launcher only, it hides `VK_LAYER_reshade*` from `vkEnumerateInstanceLayerProperties`. Everywhere else, including the game, it just forwards to the real DLL in System32, so ReShade still loads in the game. If you use the Kaibz mod, turn off its `IsRemoveIdTechReshadePopUp` option.

## Install

Copy **one** of these next to `idTechLauncher.exe`: `dwmapi.dll` (default), `userenv.dll`, `dwrite.dll`, `authz.dll` or `netapi32.dll`. If another mod already uses `dwmapi.dll`, pick the next one on that list. Don't rename the files.

To uninstall, delete the file. Each launch writes `LayerPopupFix.log` in the game folder.

## Build

Needs Visual Studio with the C++ x64 tools (edit the `vcvars64.bat` path in `build.cmd` if needed).

```
build.cmd           all five into out\
build.cmd dwmapi    one only
```
