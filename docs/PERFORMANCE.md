# Demo performance

[Documentation](INDEX.md) / Maintenance

Local measurements, 5 October 2026, Windows x64 Release on a Ryzen 7 9800X3D
system with an RTX 4090 available, using SDL's D3D11 renderer. Three alternating
CPU/GPU runs per resolution, six scenes per run, 100 measured frames per scene
after 20 warm-up frames. Figures below are medians of the three run means.

The original scene rasterizer was the dominant CPU cost. The demo now renders
triangles and depth on the GPU, uploads only vertex data, and preserves SDL's
pipeline state around its 3D pass. Geometry buffers are reused. GPU initialization
failure falls back to the capped CPU renderer. This renderer belongs to the demo;
it is not part of the library loaded by a mod.

| Window | Scene | CPU renderer: scene CPU ms | GPU renderer: scene CPU ms | GPU pass ms |
|---|---|---:|---:|---:|
| 1440×900 | exploration | 6.651 | 0.381 | 0.061 |
| 1440×900 | floor | 7.735 | 0.388 | 0.062 |
| 1440×900 | ceiling | 1.767 | 0.353 | 0.057 |
| 1440×900 | scope | 6.198 | 0.371 | 0.082 |
| 1440×900 | inventory | 9.586 | 0.371 | 0.091 |
| 1440×900 | settings | 6.732 | 0.364 | 0.097 |
| 3840×2160 | exploration | 10.127 | 0.382 | 0.043 |
| 3840×2160 | floor | 11.711 | 0.374 | 0.047 |
| 3840×2160 | ceiling | 2.514 | 0.351 | 0.025 |
| 3840×2160 | scope | 9.506 | 0.367 | 0.062 |
| 3840×2160 | inventory | 15.219 | 0.365 | 0.054 |
| 3840×2160 | settings | 10.359 | 0.372 | 0.048 |

The 4K CPU path renders its 3D image at 1920×1080 and scales it; the new GPU path
renders native 4K. GyroLib `gl_update` measured about 0.001–0.005 ms with synthetic
samples. Acquisition rows in that benchmark contain no real SDL polling and do
not establish controller-input performance.

These are hidden-window, VSync-off measurements, not displayed FPS or
input-to-photon latency. GPU clock changes affect short GPU timings. The live
`performance.csv` separately measures the actual acquisition and event paths.
A subsequent physical-controller session through a Steam shortcut was captured.
The tester reported a substantial improvement. Excluding the initial partial
report, the capture contains 9,779 gameplay, 801 inventory and 576 settings frames.
Gameplay averages weighted by frame count were 0.0128 ms for acquisition,
0.0073 ms for `gl_update`, 0.4236 ms for scene CPU work and 6.9539 ms for the full
frame, including 6.4114 ms in presentation. This is roughly 144 frames/s with
VSync enabled, not an uncapped throughput measurement. The input log records
both gyro and converted mouse contributions.

The capture is not completely free of long frames: the event stage reached
202 ms in an inventory-labelled window and 48 ms in gameplay; presentation
reached 37 ms. The scene CPU maximum was 6.52 ms and acquisition/core maxima
were 0.121/0.223 ms in gameplay. Reports label each two-second window by its
final view, so they can include transitions. They do not identify the cause of
individual event or presentation stalls, nor isolate worker-thread CPU usage.
These outliers must not be presented as a verified absence of stutter.

Reproduce with `--benchmark results.csv`, `--4k` and `--cpu-scene`; details are in
[the demo guide](TPS_DEMO.md#performance-measurements). Automated pixel tests cover
near clipping, intersecting triangles, draw-order independence, SDL coexistence
and resource recreation/resizing. Demo captures were checked for scene appearance
and foreground occlusion. The local DLL configuration passes 53 tests.
