# gmsv_workshop

Server-side `steamworks.DownloadUGC` and `steamworks.FileInfo` for Garry's Mod dedicated servers.

```lua
require("workshop")

steamworks.FileInfo("160250458", function(info)
    PrintTable(info)
end)

steamworks.DownloadUGC("104607228", function(path, file)
    print(path, file)
    if path then
        game.MountGMA(path)
    end
end)
```

`FileInfo` takes an optional third argument, `extraInfo`. When it is `true`, the query asks Steam for the long description.

Copy the matching module into `garrysmod/lua/bin`:

| GMod branch | OS      | Module                        |
| ----------- | ------- | ----------------------------- |
| x64         | Linux   | `gmsv_workshop_linux64.dll`   |
| x32         | Linux   | `gmsv_workshop_linux.dll`     |
| x64         | Windows | `gmsv_workshop_win64.dll`     |
| x32         | Windows | `gmsv_workshop_win32.dll`     |

Downloaded GMAs are written to `garrysmod/cache/<id>.gma` relative to the server's working directory. The callback receives `cache/<id>.gma`. `game.MountGMA` on a current dedicated server mounts a GMA only when it sits directly in `cache/`, `data/`, or `download/`; a `cache/srcds/` subdirectory is rejected. `file.Open` does not open `.gma` files, so the callback's file handle is nil and the path is what you pass to `game.MountGMA`. The server must be logged on to Steam (`sv_setsteamaccount`).

`FileInfo` returns the same fields as the Rust module: `id`, `title`, `description`, `owner`, `previewid`, `fileid`, `tags`, `banned`, `created`, `updated`, `size`, `previewurl`, `previewsize`, `up`, `down`, `total`, `score`, and `children`. 64-bit ids are decimal strings. On failure the table contains `id` and `error` (`-1` create failed, `-2` send failed, `-3` result count, `-5` empty id, `-6` id mismatch, or a Steam `EResult`).

## Why this is not the Rust module

The Rust module imports Steamworks symbols at load time. Current dedicated servers export `SteamAPI_SteamGameServerUGC_v021` from the `libsteam_api` already mapped by `srcds`, often with local symbol visibility. A module linked against another version (published builds import `v017`) relocates that import to NULL and segfaults on the first call. Linking the soname can also map a second, uninitialized copy of `libsteam_api`.

This module does not link Steamworks. It finds the loaded library (`/proc/self/maps` + `dlopen(RTLD_NOLOAD)`, or `GetModuleHandle` on Windows), resolves `SteamAPI_SteamGameServerUGC_v021`, and refuses the call if the symbol is missing. Downloads are polled with `GetItemState`. File info is polled with `ISteamUtils::GetAPICallResult` on the server's existing Steam pipe, so the module does not install a second callback dispatcher. `SteamUGCDetails_t` uses the packed v021 layout, and string fields are read with an explicit length so a missing NUL cannot run off the end of the struct.

## Building

### Nix

```sh
nix build                 # linux64 + linux32
nix build .#linux64
nix develop               # cmake, ninja, gcc, i686 toolchain
```

### CMake

```sh
cmake --preset linux64 && cmake --build --preset linux64
ctest --test-dir build/linux64 --output-on-failure
cmake --preset linux32 && cmake --build --preset linux32
```

Windows, from an MSVC developer shell with Ninja:

```sh
cmake --preset win64 && cmake --build --preset win64
cmake --preset win32 && cmake --build --preset win32
```

The LZMA decoder under `third_party/lzma` is Igor Pavlov's public-domain LZMA SDK. Workshop `.bin` files use the same layout as `util.Compress`.
