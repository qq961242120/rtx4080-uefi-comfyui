# RTX 4080 UEFI adapter + ComfyUI measurement

Source for the AD103 16GB pre-boot adapter used in this measured MiniMax H3 workflow. Includes the v0.3.1 five-second automatic selection, the v0.3 display-release fix, hosted tests, GEMM benchmark and comparison scripts.

**This is a write-capable experimental UEFI application for an exact hardware/firmware profile.** It changes GPU runtime state before Windows; it does not flash a VBIOS or add VRAM. The source retains exact GPU, ROM, driver-code, boot-file and state checks. A different machine requires its own read-only qualification and reviewed profile. Do not remove checks to force a match.

## Measured result

| Same RTX 4080 16GB | Stock | Manual v0.3 |
|---|---:|---:|
| Full ComfyUI execution | 511.909s | 333.049s |
| Initial GPU temperature | 36 C | 37 C |
| Sampled peak temperature | 70 C | 78 C |

178.86 seconds saved; 34.94% less elapsed time. One full run per state, each with a fresh ComfyUI process. All 124 decoded RGB frames matched. Timer: execution_start through execution_success, including model loading, text encoding, sampling, VAE and export. The MP4 containers had different hashes.

Both runs: RTX 4080 AD103 16GB, Windows 10 build19045, NVIDIA581.29,64GB RAM,320W configured power limit. The ComfyUI pair recorded temperature, not a continuous power/clock trace.

The automatic v0.3.1 startup was tested separately with numerical/GEMM checks. The full video pair used manual v0.3.

## Workload

- Video model: `minimax_h3_fl2va_pruned_fp8_Q4_0.gguf`
- Text encoder: `qwen3vl_32b_minimax_h3_nvfp4_awq.safetensors`
- VAE: `minimax_h3_video_vae_fp16.safetensors`
- Loader: `UnetLoaderGGUFDynamicVRAM`
- Feed-forward: `MiniMaxH3ChunkFeedForwardT8Advanced`, chunks=4, seq_threshold=4096
- 1024x576,124frames,24fps,25steps,res_multistep,simple,denoise1,seed2026101701

## What changed

1. v0.1 failed with EFI_NOT_READY after releasing the display.
2. v0.2 logging isolated aggregate GOP MaxMode changing14to1 after physical GOP detach.
3. v0.3 accepted that observed transition at that stage, keeping the remaining graph, interface, framebuffer and state guards.
4. v0.3.1 adds a five-second default: one unlock attempt; U runs immediately, S/Esc skips. After a mutating failure it halts; it never retries automatically or falls through to Windows.

The hardware-tested path was cold boot from USB. This release contains source, not a ready-to-boot EFI, deployment script or firmware bundle.

## Local compile check (no GPU access)

Python3 and an existing Clang are required. On the tested macOS host:

```sh
python3 build_adapter.py --host-test-profile --objects-only
python3 test_host.py --suite app_flow
python3 test_host.py --suite app_flow --sanitize
```

`--host-test-profile` uses a synthetic partition identifier for compilation and simulation. Never deploy such an output. Compiling objects does not execute UEFI code.

For local linking with an existing LLVM linker, omit `--objects-only` and supply `--linker /path/to/lld-link`. No toolchain is downloaded or installed by this script.

For a qualified local build, copy `boot_profile.example.h` to ignored `local/boot_profile.h` and configure the reviewed partition identifier. The other exact-match guards in `profile.c`, `driver_pins.h` and `boot_io.c` still apply. This is not a general-purpose installer.

## Full hosted suites

The remaining suites require private fixture bytes, supplied by the operator. They are not in this repository. Set `GU_FIXTURE_ROOT` to your local fixture directory; the expected relative layout is:

```text
UEFI-Captures/third-boot-v4-20261026/
  physical-prom.bin
  ... captured driver-code fixture files used by tests/test_display_release.c
offline-stock-assets/
  bootmgfw.efi
  derived/
    booter_load_ad102.bin
    v67-ad102-610.43.03-four-plm.bin
    gsp_ga10x.bin
    gsp_boot_ad102.bin
    gsp_boot_desc_ad102.bin
```

Then run `python3 test_host.py` and `python3 test_host.py --sanitize`. Every EFI/PCI/MMIO/DMA operation in these hosted tests is simulated. The fixture inputs and generated build/log files stay local and are ignored by Git.

## Measurement scripts

`benchmark.py` runs on Windows with Python3.11+, NumPy and an existing CUDA12-enabled PyTorch installation (including cublas64_12.dll). `benchmark.py --help` and `compare.py --help` document the inherited GEMM benchmark interface. GEMM measures a different workload from the ComfyUI full-video pair. Models, ComfyUI custom nodes and their licences are separate dependencies; no model weights are included.

## Public package and provenance

This repository was assembled from the tested v0.3.1 source on2026-10-27. Publication changes: remove local machine configuration and private logs; replace SSH/NAS-specific build paths with a local compile/link interface; make fixture input configurable. No transaction or display-release guards were widened. The source-only package excludes ROM dumps, NVIDIA firmware/payload binaries, boot binaries, EFI executables, local partition identifiers, network details and account credentials.

Core upstream: [JustAResearcher/gpu-unlock-ada](https://github.com/JustAResearcher/gpu-unlock-ada). Windows adaptation/reference: [impacifist/Windows-GPU-UNLOCK](https://github.com/impacifist/Windows-GPU-UNLOCK). See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md), [UPSTREAM-WINDOWS-NOTICES.md](UPSTREAM-WINDOWS-NOTICES.md), `LICENSES/` and `vendor/edk2/License.txt`. Existing copyright and licence notices are retained.

Original walkthrough: https://x.com/haolv511482/status/2114534188561084601
