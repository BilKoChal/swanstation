#!/usr/bin/env python3
# Regenerate embedded D3D12 DXBC blobs from HLSL sources.
#
# This script is NOT invoked by the core build. It is run by contributors who
# edit shader sources under data/shaders/d3d12/. The build itself only sees
# the resulting .inc files (checked into src/common/d3d12/embedded_dxbc/), so
# the libretro core has zero fxc.exe / D3DCompile dependency at build time.
#
# Mirror of tools/regen_vulkan_spirv.py for the Vulkan backend. The same
# editing workflow applies: edit .hlsl source -> rerun this script -> commit
# both .hlsl and the regenerated .inc together so the two stay in sync.
#
# Usage:
#     python3 tools/regen_d3d12_dxbc.py [--fxc PATH] [--wine PATH]
#
# Requires fxc.exe from the Windows 10 SDK. On Linux, requires Wine to run
# fxc.exe under emulation - pass the wine binary via --wine or set up
# `wine fxc.exe` to be invokable through the script's auto-detection. On
# Windows, the script auto-detects fxc.exe in the Windows 10 SDK install
# location (one of the kit / bin / x64 / fxc.exe paths) or via PATH.

import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
HLSL_DIR = REPO_ROOT / "data" / "shaders" / "d3d12"
INC_DIR = REPO_ROOT / "src" / "common" / "d3d12" / "embedded_dxbc"

# Map HLSL filename suffix to (fxc target prefix, identifier suffix). The
# first element is what we pass to fxc /T (e.g. "ps_5_0"); the second is the
# C++ identifier tail (e.g. k_<stem>_ps). Anything not in this table is
# skipped with a warning. Shader Model 5.0 matches the runtime baseline of
# D3D_FEATURE_LEVEL_11_0 which the D3D12 backend targets via the shared
# D3D11::ShaderCompiler path; see src/common/d3d11/shader_compiler.cpp.
STAGE_FROM_SUFFIX = {
    ".vs.hlsl": ("vs_5_0", "vs"),
    ".ps.hlsl": ("ps_5_0", "ps"),
    ".cs.hlsl": ("cs_5_0", "cs"),
    ".gs.hlsl": ("gs_5_0", "gs"),
}

# Templates: shader sources that produce more than one .inc output, each
# with a different combination of /D preprocessor defines. Index ordering
# here MUST match the helper in src/common/d3d12/embedded_shaders.cpp that
# picks the right blob at runtime - if you add a variant here, add the
# matching entry there too.
#
# For each entry: filename -> [(variant_suffix, [/D defines]), ...]
#
# Empty for now - this dict grows as batch / per-filter / per-pgxp variants
# get pre-baked. Foundation patches lay the infrastructure; subsequent
# patches add HLSL sources and the corresponding variant tables alongside.
TEMPLATE_VARIANTS = {}


def find_fxc(explicit_fxc, explicit_wine):
    # On Windows, look for fxc.exe in the Windows 10 SDK install location
    # first, then PATH. On Linux, require Wine + explicit --fxc pointing at
    # the Windows fxc.exe install.
    if explicit_fxc:
        fxc = Path(explicit_fxc)
        if not fxc.exists():
            sys.stderr.write(f"error: --fxc path does not exist: {fxc}\n")
            sys.exit(1)
        return ([str(fxc)] if sys.platform == "win32" else
                [explicit_wine or "wine", str(fxc)])

    # PATH lookup (Windows: fxc.exe; Linux/macOS: unusual but supported).
    for name in ("fxc.exe", "fxc"):
        path = shutil.which(name)
        if path:
            return [path]

    # Try the common Windows 10 SDK install location even when not on
    # Windows (in case the user has the SDK mounted under Wine prefix).
    sdk_roots = [
        Path("C:/Program Files (x86)/Windows Kits/10/bin"),
        Path("C:/Program Files/Windows Kits/10/bin"),
    ]
    # Add Wine-prefix style path if we have one.
    wine_prefix = os.environ.get("WINEPREFIX")
    if wine_prefix:
        sdk_roots.append(Path(wine_prefix) / "drive_c" / "Program Files (x86)" / "Windows Kits" / "10" / "bin")
        sdk_roots.append(Path(wine_prefix) / "drive_c" / "Program Files" / "Windows Kits" / "10" / "bin")

    for root in sdk_roots:
        if not root.exists():
            continue
        # SDK layout: <root>/<version>/x64/fxc.exe - pick the highest version.
        versions = sorted([p for p in root.iterdir() if p.is_dir() and p.name[0].isdigit()],
                          reverse=True)
        for v in versions:
            candidate = v / "x64" / "fxc.exe"
            if candidate.exists():
                return ([str(candidate)] if sys.platform == "win32" else
                        [explicit_wine or "wine", str(candidate)])

    sys.stderr.write(
        "error: fxc.exe not found. Install the Windows 10 SDK (or a newer\n"
        "       Windows SDK that still ships fxc.exe), or pass --fxc PATH\n"
        "       pointing at a working fxc.exe binary. On Linux pass --wine\n"
        "       PATH if `wine` isn't on PATH.\n")
    sys.exit(1)


def sanitize_identifier(stem):
    # Map "vram_read.ps" -> "vram_read_ps"; only [A-Za-z0-9_] survive.
    name = re.sub(r"[^A-Za-z0-9_]", "_", stem)
    return name


def compile_one(fxc_cmd, hlsl_path, target, defines=None):
    # fxc /O3 /T <target> /E main /Fo <out> <in> [/D KEY=VALUE]...
    # Output to a temp .dxbc.tmp under INC_DIR so we don't pollute /tmp on
    # restricted-filesystem hosts. Matches the regen_vulkan_spirv.py pattern.
    dxbc_path = INC_DIR / (hlsl_path.stem + ".dxbc.tmp")
    cmd = list(fxc_cmd) + [
        "/T", target,
        "/E", "main",
        "/O3",
        "/nologo",
        "/Fo", str(dxbc_path),
        str(hlsl_path),
    ]
    for d in defines or ():
        cmd.append(f"/D{d}")
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        sys.stderr.write(f"fxc failed for {hlsl_path.name}:\n")
        sys.stderr.write(result.stdout)
        sys.stderr.write(result.stderr)
        if dxbc_path.exists():
            dxbc_path.unlink()
        sys.exit(1)
    data = dxbc_path.read_bytes()
    dxbc_path.unlink()
    if len(data) < 4:
        sys.stderr.write(
            f"error: {hlsl_path.name} produced {len(data)} bytes of output "
            f"(too short to be DXBC)\n")
        sys.exit(1)
    # DXBC magic: "DXBC" = 0x43425844 little-endian in the first 4 bytes.
    magic = struct.unpack("<I", data[:4])[0]
    if magic != 0x43425844:
        sys.stderr.write(
            f"error: {hlsl_path.name} produced output with bad magic "
            f"(0x{magic:08x}, expected 0x43425844)\n")
        sys.exit(1)
    return data


def emit_inc(identifier, hlsl_name, target, defines, data, out_path):
    # 16 bytes per line keeps the file diff-friendly without being too tall.
    PER_LINE = 16
    lines = []
    define_summary = " ".join(f"/D{d}" for d in defines) if defines else "(no /D)"
    lines.append(
        f"// Autogenerated from data/shaders/d3d12/{hlsl_name}.\n"
        f"// DO NOT EDIT. Regenerate with tools/regen_d3d12_dxbc.py.\n"
        f"// fxc /T {target} /E main /O3 {define_summary}\n"
        f"//\n"
        f"// Included inside namespace D3D12::EmbeddedShaders in\n"
        f"// embedded_shaders.cpp. The matching extern declaration lives in\n"
        f"// embedded_shaders.h.\n"
        f"\n"
        f"const uint8_t k_{identifier}[] = {{\n")
    for i in range(0, len(data), PER_LINE):
        chunk = data[i:i + PER_LINE]
        bytes_str = ", ".join(f"0x{b:02x}" for b in chunk)
        lines.append(f"  {bytes_str},\n")
    lines.append(f"}};\n")
    lines.append(f"const size_t k_{identifier}_size_bytes = sizeof(k_{identifier});\n")
    out_path.write_text("".join(lines))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fxc", help="path to fxc.exe (auto-detected if omitted)")
    parser.add_argument("--wine", help="path to wine binary (default: 'wine'); ignored on Windows")
    args = parser.parse_args()

    if not HLSL_DIR.exists():
        sys.stderr.write(f"error: HLSL source directory missing: {HLSL_DIR}\n")
        sys.exit(1)

    INC_DIR.mkdir(parents=True, exist_ok=True)

    # Discover all .hlsl sources. Skip files that don't match a known
    # stage suffix.
    hlsl_files = []
    for entry in sorted(HLSL_DIR.iterdir()):
        if not entry.is_file():
            continue
        for suffix, (target, ident_tag) in STAGE_FROM_SUFFIX.items():
            if entry.name.endswith(suffix):
                stem = entry.name[: -len(suffix)]
                hlsl_files.append((entry, target, ident_tag, stem))
                break
        else:
            if entry.suffix == ".hlsl":
                sys.stderr.write(f"warning: skipping {entry.name} (no known stage suffix)\n")

    if not hlsl_files:
        # Foundation case: no .hlsl files yet, just the README. Print a
        # neutral message and exit zero - the .inc directory is set up,
        # nothing to do until a shader source lands. Don't even look for
        # fxc.exe yet, since the script has no work that needs it.
        print(f"no .hlsl sources under {HLSL_DIR.relative_to(REPO_ROOT)}; nothing to do.")
        return

    fxc_cmd = find_fxc(args.fxc, args.wine)

    total = 0
    for hlsl_path, target, ident_tag, stem in hlsl_files:
        ident_base = sanitize_identifier(stem)
        variants = TEMPLATE_VARIANTS.get(hlsl_path.name)
        if variants is None:
            # No variants table for this file - compile once with no /D
            # defines, emit a single .inc.
            data = compile_one(fxc_cmd, hlsl_path, target)
            identifier = f"{ident_base}_{ident_tag}"
            out_path = INC_DIR / f"{ident_base}_{ident_tag}.inc"
            emit_inc(identifier, hlsl_path.name, target, [], data, out_path)
            total += 1
        else:
            # Template: emit one .inc per (variant_suffix, defines) row.
            for variant_suffix, defines in variants:
                data = compile_one(fxc_cmd, hlsl_path, target, defines)
                identifier = f"{ident_base}_{ident_tag}_{variant_suffix}"
                out_path = INC_DIR / f"{ident_base}_{ident_tag}_{variant_suffix}.inc"
                emit_inc(identifier, hlsl_path.name, target, defines, data, out_path)
                total += 1

    print(f"regenerated {total} .inc file(s) under {INC_DIR.relative_to(REPO_ROOT)}")


if __name__ == "__main__":
    main()
