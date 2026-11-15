#!/bin/bash
# tools/check_windows_compile.sh: compile-checks the runtime's C files for Windows x64 (clang, from the ziglang pip
# package), without the Android SDK, NDK or the game's files. It is the first step of the PC build (docs/PC-BUILD.md):
# the files that compile here are the ones the Windows runtime can share with the Android one.
#
#   tools/check_windows_compile.sh            # all runtime C files; exits 1 if a file outside EXPECTED fails
#   tools/check_windows_compile.sh -v         # also prints each file's first errors
#
# Downloads (into $CACHE, default build/winchk, not committed): SDL3 3.4.16, Vulkan-Headers and volk (sdk-1.4.350.1),
# VulkanMemoryAllocator 3.3.0 and shaderc's headers (v2026.3), all from codeload.github.com, and the ziglang pip package
# (clang for Windows). Their hashes are not pinned here (the Android build's android/deps/build_deps.sh pins the release
# assets); the headers are only compiled against, never linked.
set -euo pipefail
R="$(cd "$(dirname "$0")/.." && pwd)"
CACHE="${CACHE:-$R/build/winchk}"
VERBOSE=0; [ "${1:-}" = "-v" ] && VERBOSE=1
mkdir -p "$CACHE"
cd "$CACHE"

# Files that are not expected to compile for Windows yet, and why:
#   runtime/platform/android.c        the Android host layer (the Windows one is the next step of the PC build)
#   runtime/nvn/vulkan/android_driver.c  Android's driver loader (adrenotools): the desktop loads the Vulkan loader
#   runtime/services/hwopus.c, runtime/hle/movie.c  FFmpeg's headers (not yet fetched for Windows)
EXPECTED="runtime/platform/android.c runtime/nvn/vulkan/android_driver.c runtime/services/hwopus.c runtime/hle/movie.c"

fetch() {  # name url dir (strip 1), then the dir is ready
    local name="$1" url="$2" dir="$CACHE/$1"
    if [ ! -d "$dir" ]; then
        curl -fsSL -o "$CACHE/$name.tgz" "$url"
        mkdir -p "$dir" && tar -xzf "$CACHE/$name.tgz" -C "$dir" --strip-components 1
    fi
}
fetch sdl3 https://codeload.github.com/libsdl-org/SDL/tar.gz/refs/tags/release-3.4.16
fetch vkh https://codeload.github.com/KhronosGroup/Vulkan-Headers/tar.gz/refs/tags/vulkan-sdk-1.4.350.1
fetch volk https://codeload.github.com/zeux/volk/tar.gz/refs/tags/vulkan-sdk-1.4.350.1
fetch vma https://codeload.github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/tar.gz/refs/tags/v3.3.0
if [ ! -f "$CACHE/shaderc/libshaderc/include/shaderc/shaderc.h" ]; then
    curl -fsSL -o "$CACHE/shaderc.tgz" https://codeload.github.com/google/shaderc/tar.gz/refs/tags/v2026.3
    mkdir -p "$CACHE/shaderc" && tar -xzf "$CACHE/shaderc.tgz" -C "$CACHE/shaderc" --strip-components 1 --wildcards '*/libshaderc/include/*'
fi

INC="$CACHE/inc"
rm -rf "$INC" && mkdir -p "$INC/vma" "$INC/vulkan" "$INC/shaderc"
cp -r "$CACHE/vkh/include/vulkan" "$CACHE/vkh/include/vk_video" "$INC/"
cp "$CACHE/volk/volk.h" "$CACHE/volk/volk.c" "$INC/"
cp "$CACHE/vma/include/vk_mem_alloc.h" "$INC/vma/"
cp -r "$CACHE/shaderc/libshaderc/include/shaderc/." "$INC/shaderc/"

# smaa_hlsl.inc: the SMAA shader as a C string, made as android/totkrecompandroid/app/build.gradle.kts makes it
python3 - "$R/third_party/smaa/SMAA.hlsl" "$INC/smaa_hlsl.inc" <<'PY'
import sys
src, out = sys.argv[1], sys.argv[2]
b = bytes(c if c >= 0 else ord('?') for c in open(src, 'rb').read() if c != 13)
b = bytes(c if c < 128 else ord('?') for c in b)
lines = b.decode('ascii').split('\n')
if lines and lines[-1] == '':
    lines = lines[:-1]
with open(out, 'w') as f:
    for line in lines:
        f.write('"' + line.replace('\\', '\\\\').replace('"', '\\"') + '\\n"\n')
PY

ZIGDIR="$CACHE/ziglang"
if [ ! -d "$ZIGDIR" ]; then python3 -m pip install -q --target "$ZIGDIR" ziglang; fi
export PYTHONPATH="$ZIGDIR" ZIG_GLOBAL_CACHE_DIR="$CACHE/zig-global" ZIG_LOCAL_CACHE_DIR="$CACHE/zig-local"
mkdir -p "$CACHE/obj"

fail=0; total=0; ok=0
for f in "$R"/runtime/*.c "$R"/runtime/hle/*.c "$R"/runtime/services/*.c "$R"/runtime/nvn/*.c \
         "$R"/runtime/nvn/vulkan/*.c "$R"/runtime/platform/*.c; do
    rel="${f#$R/}"
    total=$((total + 1))
    log="$CACHE/obj/$(echo "$rel" | tr '/' '_').log"
    if python3 -m ziglang cc -target x86_64-windows-gnu -std=gnu11 -c -o "$CACHE/obj/$(echo "$rel" | tr '/' '_').o" \
         -I"$R/runtime" -I"$R/runtime/nvn" -I"$CACHE/sdl3/include" -I"$INC" -I"$INC/vma" \
         -I"$R/third_party/zstd" -I"$R/third_party/smaa/Textures" -I"$CACHE/inc" \
         -D_CRT_SECURE_NO_WARNINGS -Wall -Wno-unused-function -Wno-format -Wno-unused-variable -Wno-unused-but-set-variable \
         "$f" > "$log" 2>&1; then
        ok=$((ok + 1))
    else
        if echo " $EXPECTED " | grep -q " $rel "; then
            echo "expected: $rel ($(grep -c 'error:' "$log") error)"
        else
            echo "FAILED:   $rel ($(grep -c 'error:' "$log") errors)"
            fail=1
        fi
        [ "$VERBOSE" = 1 ] && grep 'error:' "$log" | head -5 | sed 's/^/    /'
    fi
done
echo "compiled for Windows: $ok of $total runtime files"
exit $fail
