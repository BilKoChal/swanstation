# D3D12 shader sources (pre-baked DXBC)

This directory holds the HLSL sources for shaders used by the D3D12 backend
that have been moved off the runtime `ShaderGen` path. Each `.hlsl` file here
has a matching pre-compiled DXBC blob checked in to
`src/common/d3d12/embedded_dxbc/`.

## Why this exists

Historically the D3D12 backend generated HLSL at runtime via
`src/core/shadergen.cpp` and `src/core/gpu_hw_shadergen.cpp`, then handed the
text to `D3DCompile` (the in-process HLSL -> DXBC compiler from `d3dcompiler_47.dll`)
to produce a DXBC blob for each PSO. On a cold shader cache, walking the
1164-cell batch pipeline matrix in `precompile_sync` mode takes on the order
of one minute, which is the worst entry in the post-dim-cache cost matrix
(see the cost-matrix comments in 10c53b8, 57ac62e, and the patches that
followed). The runtime cache hides this on warm boots, but cold boots and
shader cache invalidations re-pay the full per-cell `D3DCompile` cost every
time the user changes a setting that hits the full-flush branch.

Pre-baking the DXBC removes:

* per-launch HLSL compilation stall on shader cache misses,
* a class of "stale cache after `d3dcompiler_47.dll` version drift" failures,
* the implicit dependency on `D3DCompile` being available at runtime for
  the cells that have been pre-baked.

The OpenGL / OpenGL ES / D3D11 / Vulkan backends still use the runtime
`ShaderGen` path; only the D3D12 backend reads from the embedded DXBC. (The
Vulkan equivalent of this infrastructure lives at `data/shaders/vulkan/` /
`src/common/vulkan/embedded_spirv/` / `tools/regen_vulkan_spirv.py`; this
directory is the mirror.)

## Build-time constraint

The core build (`Makefile.libretro` and `src/common/CMakeLists.txt`) must
never invoke `fxc.exe` or any other external shader compiler. The `.inc`
files under `src/common/d3d12/embedded_dxbc/` are checked into the
repository and compiled like any other C++ source. The
`tools/regen_d3d12_dxbc.py` helper exists for contributors who edit the
`.hlsl` sources in this directory; it is not part of the build.

## Regenerating the DXBC blobs

After editing a `.hlsl` file here, run:

    python3 tools/regen_d3d12_dxbc.py

This invokes `fxc.exe` (Shader Model 5.0, `/O3`, `/E main`) on each `.hlsl`
file and rewrites the matching `.inc` under `src/common/d3d12/embedded_dxbc/`.
Both the `.hlsl` and the regenerated `.inc` must be committed together so the
two stay in sync.

`fxc.exe` ships with the Windows 10 SDK and is **not** invoked by the core
build. On Linux, run the script under Wine - it will look for `fxc.exe` in
the standard SDK install path under `$WINEPREFIX/drive_c/Program Files (x86)/Windows Kits/10/bin/<version>/x64/fxc.exe`,
or you can pass `--fxc PATH` explicitly. The `--wine PATH` flag lets you
point at a specific wine binary if `wine` isn't on `PATH`.

## Filename convention

Stage is encoded in the suffix:

* `.vs.hlsl` -> vertex shader  (compiled as `vs_5_0`)
* `.ps.hlsl` -> pixel shader   (compiled as `ps_5_0`)
* `.cs.hlsl` -> compute shader (compiled as `cs_5_0`)
* `.gs.hlsl` -> geometry shader (compiled as `gs_5_0`)

The `_5_0` shader model targets `D3D_FEATURE_LEVEL_11_0`, matching the
runtime baseline of the D3D12 backend (see the shared compile path in
`src/common/d3d11/shader_compiler.cpp` which both D3D11 and D3D12 used
prior to the pre-bake migration).

## Template variants

Shaders that need multiple pre-baked variants (e.g. one per `BatchRenderMode`
or per `GPUTextureFilter`) are listed in the `TEMPLATE_VARIANTS` dict at the
top of `tools/regen_d3d12_dxbc.py`. Each entry maps an `.hlsl` filename to
the list of `(variant_suffix, [/D defines])` tuples that the regen script
should produce.

If you add a template variant to the dict, add the matching extern
declaration to `src/common/d3d12/embedded_shaders.h` AND the matching slot
lookup to `src/common/d3d12/embedded_shaders.cpp` - the index ordering at
the runtime call site must match the order in the dict so the correct
blob gets bound at PSO-creation time.
