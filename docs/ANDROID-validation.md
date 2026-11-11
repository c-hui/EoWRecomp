# Android Adreno validation

Validated on **Adreno 840 / Android 17 / Vulkan 1.4.295** using the supported v1.0.0 game build. A private Windows game library reproduced the renderer failures on **RTX 5060 Ti / Vulkan**. Shared Maxwell emitter changes were tested through Vulkan; **Metal was not tested**.

## Failures and fixes

| Symptom | Cause and resulting behavior |
| --- | --- |
| `VK_ERROR_FEATURE_NOT_PRESENT` during device creation | `uniformAndStorageBuffer8BitAccess` was requested although the renderer uses 32-bit uniform blocks. Removing this unused request allowed device creation. A device probe returned -8 before and 0 after. Initial Android surface creation also waits for an available native window. |
| Dark vertical strip from Link to the top of the room | `LDC.64 R6:R7,c[6][R6+40]` wrote R6 before calculating the second address. The fourth bone's Z translation was lost, stretching two four-weight meshes and contaminating depth/AO. Snapshot the overlapping address register before both loads. Normal shadows remain enabled. |
| Red rectangle behind the hearts | NVN RGB565 stores R in the low five bits. Vulkan PACK16 names fields from the high bits, requiring `B5G6R5_UNORM_PACK16`. Red/blue samples and the game's transparency decision are now correct. Other packed formats keep their preceding mappings. |
| Glyphs absent on the phone although icons render | Glyph vertex shaders fetch per-character storage data with LDG. The phone lacks `VK_EXT_external_memory_host`; the preceding path bound zeros. Read-only graphics loads now consult a per-draw/per-stage table and snapshot their valid bound NVN storage ranges, bounded by 32 MiB and the device's storage-buffer limit. This avoids copying the whole guest heap. Native import and compute copy-back remain available as before. |
| Chinese mode falls back to Japanese, or lacks glyphs | Serve modern `zh-Hans` / `zh-Hant`, accepting old `zh-CN` / `zh-TW` settings. Bundle an OFL-licensed static TrueType Noto Sans SC fallback, with source/hash/tool-version pins and the font license. Explicit fonts and console dumps retain precedence. A mutex prevents concurrent Address/Size requests from observing an unfinished font load. |
| Incompatible/damaged pipeline cache supplied to the driver | Bound disk-cache size and validate the native cache header, GPU identity and UUID before loading it. Keep atomic replacement on POSIX and include the shader optimization option in SPIR-V cache identity. |

The build changes use explicit bash for the game-library ID script, portable CMake SMAA embedding, and an optional NDK version override. The Python extractor also accepts common tickets/title.keys when reading the user's own rights-managed package.

The cache separation, packed-format/instruction investigation and pinned open-font conversion approach were informed by [S2Arm](https://github.com/jhaizhou-ops/S2Arm/tree/3fe7ea9d1df517cfc011667fcdc5818b79e2e226). No S2Arm renderer was integrated. The font is open Noto data, not a console font; see its [source and license](../third_party/fonts/NotoSansSC/README.md).

## Regression checks

The Vulkan tests use the production format mapper or Maxwell translator, shaderc, and actual GPU dispatch/sampling. The global-memory test translates **vertex-stage** instructions and observes them in a compute probe with no vertex inputs.

| Check | Observed result |
| --- | --- |
| `test_vk_ldc` | Before: `11111111 00000000`; after: `11111111 22222222`. |
| `test_vk_packed` | Before: red/green/blue sampled as blue/green/red; after: all three correct. |
| `test_vk_gmem` | Zero native heap previously returned zero; staged range returns `1234abcd`. Empty-map/native-heap control also returns `1234abcd`. |
| `test_pipeline_cache` | Valid native header accepted; truncation, oversize, version/GPU/UUID mismatch rejected. |
| `test_shared_fonts` | Original second request could observe size 0. Both threads now receive the same complete font. |
| `test_language` | Legacy/modern simplified/traditional codes, English and CRLF passed (six configurations). |
| `test_cjk_font.py` | 19 prompt/UI characters have real TrueType outlines; 30,890 mapped characters. Two pinned rebuilds produced identical font bytes. |

Example builds on a host with Vulkan/shaderc development libraries:

```sh
cc -O2 tools/test_vk_ldc.c runtime/nvn/maxwell/translate.c -lvulkan -lshaderc_shared -o build/test_vk_ldc
cc -O2 tools/test_vk_gmem.c runtime/nvn/maxwell/translate.c -lvulkan -lshaderc_shared -o build/test_vk_gmem
cc -O2 tools/test_vk_packed.c -lvulkan -lshaderc_shared -o build/test_vk_packed
cc -O2 tools/test_pipeline_cache.c -o build/test_pipeline_cache
cc -O2 -march=native -pthread -DEOW_RUNTIME tools/test_shared_fonts.c -o build/test_shared_fonts
cc -O2 -march=native -DEOW_RUNTIME tools/test_language.c -o build/test_language

build/test_vk_ldc
build/test_vk_gmem
build/test_vk_packed
build/test_pipeline_cache
build/test_shared_fonts third_party/fonts/NotoSansSC/EowSansSC-Regular.ttf
printf 'language = zh-CN\n' > build/test_language.ini
build/test_language build/test_language.ini zh-Hans
python tools/test_cjk_font.py
```

The executed Windows checks used LLVM-MinGW and the project's Windows compatibility header; Android compiled with NDK 28.1.13356709. The font builder requires `fonttools==4.67.0` and verifies its downloaded open-font sources before conversion.

## Device acceptance

The same-signature replacement APK loaded the original save. File-selection labels and the dungeon's **挥剑** prompt were readable, hearts had transparent surroundings, and the central strip disappeared after movement. Touch controls and background/foreground resume worked. All four save files were byte-identical to their pre-install backup. This is opening-scene/menu validation, not a full-playthrough or performance claim.

Before:

![Adreno opening scene before fixes](screenshots/android-adreno840-before.jpg)

After:

![Adreno opening scene with transparent HUD and readable Chinese prompt](screenshots/android-adreno840-after.jpg)

## Remaining limitations

- The packed-RomFS setup's FFmpeg path still needs physical video files. During device validation, the user's own 19 WebM files were materialized into their private game-data directory to resolve the black title. Those files, game binaries, generated code, keys, saves and the personal APK are **not included** in this contribution. A general virtual-RomFS FFmpeg reader is separate work.
- Graphics snapshots are read-only; this does not implement general mobile GPU-write/compute fallback. Existing unsupported compute instructions remain.
- Some Windows replay runs, including an original-font control, blocked in guest initialization. No fix for those waits is claimed. The documented full-scene no-import comparison completed before/after with a 3 ms pacing hook.
- No sustained FPS improvement, thermal result or Metal result is claimed.
