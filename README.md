# Super Hexagon — PS Vita Port

### → To install, go to **[superhexagon.mcallbos.co](https://superhexagon.mcallbos.co/)** ←

Drop in your Android APK in the browser and it'll hand you back
`superhexagon.zip` containing the ready-to-install VPK and a `superhexagon/`
data folder. Nothing leaves your machine. This repo is the source code behind
that site and the loader VPK it builds.

You can buy Super Hexagon from
**[Terry Cavanagh on itch.io](https://terrycavanagh.itch.io/super-hexagon)**.

---

A homebrew `.so`-loader port of **Super Hexagon** (Android) to the PlayStation
Vita, plus a browser-based installer that turns your user-supplied APK into a
Vita-ready install package.

> **This repository contains only the loader and the website. It does not
> contain, distribute, or link to Super Hexagon itself.** The game, its assets,
> and the Android binaries are owned by their original rights holders. To play,
> you must legally own the Android version and supply the APK from your own copy.

## What's in here

| Path | What it is |
| --- | --- |
| `source/` | Vita-side loader: relocates the Android `.so` files, stubs Android/JNI APIs, maps controls, handles audio, text, saves, and rendering. |
| `source/reimpl/native_audio.c` | Vita-native music and sound-effect playback. |
| `source/reimpl/native_text.c` | Native font/text replacement and UI-specific hooks. |
| `source/reimpl/native_input.c` | Vita input state hooks for the Android game. |
| `lib/{falso_jni,fios,kubridge,libc_bridge,sha1,so_util}/` | Standard `.so`-loader support libraries. |
| `extras/livearea/` | Vita LiveArea template and packaging assets. |
| `docs/` | The website. Drop in your APK -> get a VPK + data folder in your browser. Nothing leaves your machine. |
| `.github/workflows/` | CI that builds the loader VPK and publishes the site to GitHub Pages on pushes to `main`/`master`. |

## For end users

Don't build from source. Visit
**[superhexagon.mcallbos.co](https://superhexagon.mcallbos.co/)** and drop in
your Super Hexagon Android APK. You'll get back `superhexagon.zip`. Unzip it and
you'll have:

- `superhexagon_vita.vpk` — install via VitaShell
- `superhexagon/` — FTP or copy this folder to `ux0:data/`

Your Vita should end up with:

```text
ux0:data/superhexagon/assets/
ux0:data/superhexagon/libsuperhexagon.so
ux0:data/superhexagon/libopenFrameworksAndroid.so
ux0:data/superhexagon/liboboe.so
ux0:data/superhexagon/libc++_shared.so
```

Runtime requirements:

- A hacked PS Vita or PSTV
- `kubridge.skprx`
- `libshacccg.suprx`

## For developers — building the loader

Requirements: [VitaSDK-softfp](https://github.com/vitasdk-softfp) with the
`VITASDK` environment variable pointing at the install root.

```sh
git clone --recurse-submodules git@github.com:mcallbosco/superhexagon-vita.git
cd superhexagon-vita

cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DSTANDALONE_DEMO=OFF
cmake --build build -j"$(nproc)"
```

Output:

```text
build/superhexagon_vita.vpk
build/eboot.bin
```

The built VPK is loader-only; it does not contain game data. Use the website or
provide the expected files under `ux0:data/superhexagon/`.

### Common CMake flags

| Flag | Values | Default | Effect |
| --- | --- | --- | --- |
| `STANDALONE_DEMO` | `ON`, `OFF` | `ON` | Builds a tiny native demo instead of the real Android `.so` loader. Release builds for the port should use `OFF`. |
| `DATA_PATH` | path | `ux0:data/superhexagon/` | Where the loader looks for extracted game data on the Vita. |
| `SO_PATH` | path | `${DATA_PATH}libsuperhexagon.so` | Main Android game library path. |
| `USE_SCELIBC_IO` | `ON`, `OFF` | `ON` | Route file IO through `SceLibcBridge`/FIOS. |
| `SHADER_FORMAT` | `GLSL`, `CG`, `GXP` | `GLSL` | Shader source/cache format used by the loader. |
| `DUMP_COMPILED_SHADERS` | `ON`, `OFF` | `ON` | Writes compiled shader artifacts to the data folder for debugging/cache work. |

## Notes

- Title ID: `SHXV00001`
- LiveArea title: `Super Hexagon`
- Data path: `ux0:data/superhexagon/`
- Save files are stored in the same data folder.

The loader does not contain the commercial game and is not useful without a
legally obtained Android copy.

## Credits

- **Terry Cavanagh** — Super Hexagon
- **TheFloW** — original Vita `.so`-loader research
- **Rinnegatamante** — VitaGL and Vita porting foundations
- **FalsoJNI / so_util contributors** — Android loader support libraries
- **soloader-boilerplate contributors** — project foundation this port was built from
- **VitaSDK — SoftFP contributors** — toolchain support for Android `.so` interop
- **Mcall** — this Vita port
