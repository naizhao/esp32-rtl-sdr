# esp32-rtl-sdr (naizhao fork)

🌐 Official Website: **[air.club](https://air.club)**

Fork of [kvhnuke/esp32-rtl-sdr](https://github.com/kvhnuke/esp32-rtl-sdr) used
as a git submodule by the **[Pilot-Kit-Box-ESP32-P4](https://github.com/naizhao/Pilot-Kit-Box-ESP32-P4)**
firmware — part of the [air.club](https://air.club) Pilot Kit avionics
ecosystem.

## ⚠ Active branch lives elsewhere

The `main` branch here is a frozen project skeleton — it is **not** the
maintained codebase. All ongoing work targets the ESP32-P4 + Waveshare
ESP32-P4-WIFI6 board and lives on:

> ### → [`pilot-kit-box-esp32p4`](https://github.com/naizhao/esp32-rtl-sdr/tree/pilot-kit-box-esp32p4)

That branch contains:

- Restructured as a pure ESP-IDF component (consumed via `EXTRA_COMPONENT_DIRS`).
- Async IQ streaming ported to the ESP32-P4 USB Host stack — `rtlsdr_read_async()`
  delivers URB completions on the caller's task (no DSP in the ISR / USB
  callback context, per Pilot Kit Box SPEC red line #3).
- RTL2832U + **FC0013** tuner detection re-enabled in `librtlsdr.c` (upstream
  had it gated out).
- URB length aligned to USB High-Speed MPS — `DEFAULT_BUF_LENGTH = 12 * 512`
  (6144 B), replacing the broken 6400-byte default that silently stalled HS.

Open issues / PRs touching the P4 path should land **there**, not on `main`.

## Lineage

```
kvhnuke/esp32-rtl-sdr (upstream)
        │
        ▼
naizhao/esp32-rtl-sdr  main                       ◄── you are here (skeleton, frozen)
                       └── pilot-kit-box-esp32p4  ◄── active integration branch
```

Previous short-lived branches (`feat/p4-async-iq-stream`, `esp32p4`) have been
folded into `pilot-kit-box-esp32p4` and removed.
