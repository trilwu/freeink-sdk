# EPD_Painter (vendored)

Source: https://github.com/juicecultus/crosspoint-reader-papers3
Path in source: `lib/EPD_Painter/`
Commit: d9792a58b415f37e6f9de688241467db85048d05
Vendored: 2026-07-29

Purpose-built ED047TC1 driver for the M5Paper S3. Replaces LovyanGFX's generic
Panel_EPD, which has no lighter/darker waveform asymmetry and reaches the panel
only through a per-bit unpack that destroys antialiasing.

Vendored rather than submoduled because it is a single-board driver with no
upstream release cadence. Files are byte-identical to the source except where
noted below.

## Local modifications
(none yet)
