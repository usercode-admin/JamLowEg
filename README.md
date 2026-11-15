# JamLowEg

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://opensource.org/licenses/MIT)
[![Platform: ESP32](https://img.shields.io/badge/Platform-ESP32--C3%20%2F%20S3-red.svg)](https://www.espressif.com/)
[![Transceiver: nRF24L01+](https://img.shields.io/badge/Transceiver-nRF24L01%2B-green.svg)](https://www.nordicsemi.com/)
[![Language: C++](https://img.shields.io/badge/Language-C%2B%2B-00599C.svg)](https://isocpp.org/)

**JamLowEg** is an ultra-low-latency, high-speed 2.4 GHz Continuous Wave (CW) frequency sweeper engineered for **ESP32** microcontrollers paired with the **nRF24L01+** RF transceiver. By bypassing standard Arduino SPI abstraction layers and directly manipulating hardware registers, this firmware achieves near-theoretical-limit channel hopping rates across the 2.4 GHz ISM band.

> **⚠️ Legal & Educational Disclaimer**  
> This project is developed strictly for **educational purposes, RF spectrum analysis, hardware benchmarking, and authorized laboratory testing** within shielded environments (e.g., Faraday cages). Transmitting unmodulated continuous wave interference on public 2.4 GHz frequencies (Wi-Fi, Bluetooth, Zigbee) may violate local telecommunications regulations (such as FCC, CE, or MIC rules). The author assumes no responsibility or liability for misuse or unlawful operation.

---

## Key Features

- **Bare-Metal Register Manipulation:** Directly accesses ESP32 `SPI2` peripheral registers (`SPI2_DATA_BUF0`, `SPI2_CMD_REG`, `SPI2_MS_DLEN`) and atomic GPIO set/clear registers (`GPIO.out_w1ts`, `GPIO.out_w1tc`) to eliminate HAL/driver overhead.
- **16-Bit Single-Transaction Channel Hopping:** Packs the `W_REGISTER` command (`0x25`) and the target RF channel byte into a single 16-bit SPI frame (`SPI2_MS_DLEN = 15`), reducing bus transaction overhead by 50%.
- **IRAM Execution & Aggressive Compiler Optimization:** Places the critical sweep loop inside Instruction RAM (`IRAM_ATTR`) with `-O3 -funroll-loops` flags to prevent flash cache misses and eliminate branch penalties.
- **Hardware Continuous Carrier Wave Mode:** Configures the nRF24L01+ into `CONT_WAVE` + `PLL_LOCK` test mode at maximum output power (`0 dBm`, 2 Mbps air data rate).
- **Deterministic FreeRTOS Task Pinning:** Runs as a high-priority task (`Priority 20`) pinned to Core 0 with periodic watchdog yields (`YIELD_EVERY = 4096`) to ensure real-time determinism alongside system stability.

---

## Hardware Architecture & Pinout

The default pin definitions target modern RISC-V and Xtensa ESP32 variants featuring remappable SPI2 (FSPI) peripheral matrices, such as the **ESP32-C3** and **ESP32-S3**.

| Signal | ESP32 GPIO | nRF24L01+ Pin | Description |
| :--- | :---: | :---: | :--- |
| **SCK** | `GPIO 6` | `SCK` | SPI Clock (12 MHz) |
| **MISO** | `GPIO 2` | `MISO` | Master In Slave Out |
| **MOSI** | `GPIO 7` | `MOSI` | Master Out Slave In |
| **CE** | `GPIO 3` | `CE` | Chip Enable (Held `HIGH` in CW mode) |
| **CSN** | `GPIO 10` | `CSN` | SPI Chip Select (Active `LOW`) |
| **VCC** | `3.3V` | `VCC` | Regulated 3.3V (10µF–100µF decoupling capacitor recommended) |
| **GND** | `GND` | `GND` | Common Ground |

---

## Technical Architecture

### 1. Continuous Wave (CW) Initialization (`nrf_enter_cw`)
In accordance with the Nordic Semiconductor nRF24L01+ Product Specification (Appendix A / Register `0x06`), continuous carrier transmission is initialized via the following sequence:
1. Verify SPI bus integrity by polling the `STATUS` register (`0x07`).
2. Power up the transceiver (`CONFIG = 0x02`) and allow `2 ms` for crystal oscillator stabilization.
3. Disable packet-handling overhead: Auto-ACK (`EN_AA = 0x00`), Auto-Retransmit (`SETUP_RETR = 0x00`), RX data pipes (`EN_RXADDR = 0x00`), and dynamic payloads (`DYNPD`, `FEATURE`).
4. Configure `RF_SETUP` (`0x06`) with `PLL_LOCK` + `0 dBm` + `2 Mbps` (`0x16`), wait `15 µs`, and assert the `CONT_WAVE` bit (`0x80` -> `0x96`).
5. Assert `CE` HIGH to lock the PLL and emit an unmodulated carrier wave.

### 2. Sweep Modes
Configured via the `SWEEP_FULL` preprocessor macro at compile time:
- **`SWEEP_FULL = 1` (Default):** Sweeps all **125 channels** (`0` to `124`), spanning **2400 MHz – 2524 MHz** in 1 MHz increments.
- **`SWEEP_FULL = 0`:** Sweeps **79 channels** (`0` to `78`), targeting **2400 MHz – 2478 MHz** (overlapping standard Bluetooth and Wi-Fi channels 1–13).

### 3. SPI Bus Timing
Operating at `SPI_HZ = 12,000,000` (12 MHz—slightly overclocked above the 10 MHz datasheet rating; reduce to `10000000UL` if using unshielded jumper wires), shifting a 16-bit frame takes approximately **1.33 µs** per channel update, bounded primarily by the ~130 µs PLL settling time of the nRF24L01+ frequency synthesizer.

### 4. Compiler Optimization Flags
To maximize execution speed and reduce IRAM/Flash footprint, the build environment overrides default Arduino size-oriented flags (`-Os`) with performance-oriented GCC flags in `platform.local.txt` (or `build_flags` in PlatformIO):

```ini
compiler.optimization_flags=-O3 -funroll-loops -ffast-math -fno-exceptions -fno-rtti
compiler.optimization_flags.release=-O3 -funroll-loops -ffast-math -fno-exceptions -fno-rtti
```

| Flag | Technical Purpose |
| :--- | :--- |
| **`-O3`** | Enables the highest level of GCC speed optimization, including aggressive function inlining, vectorization, and register allocation. |
| **`-funroll-loops`** | Unrolls loops with known iteration bounds at compile time, reducing CPU branch mispredictions and loop-counter overhead during channel sweeps. |
| **`-ffast-math`** | Relaxes strict IEEE 754 floating-point compliance to allow faster arithmetic approximations when computing timing or frequency intervals. |
| **`-fno-exceptions`** | Disables C++ exception handling (`try`/`catch` unwinding tables), eliminating runtime overhead and reducing binary size. |
| **`-fno-rtti`** | Disables Run-Time Type Information (`dynamic_cast` and `typeid`), stripping unnecessary metadata from the compiled firmware. |

---

## Getting Started

### Prerequisites
- **PlatformIO** (recommended) or **Arduino IDE** with the `espressif32` platform package installed.
- An **ESP32-C3**, **ESP32-S3**, or **ESP32-C6** development board. *(Note: Original ESP32 Xtensa LX6 chips use different `DR_REG_SPI2_BASE` register offsets; verify `soc/spi_reg.h` compatibility before compiling for legacy targets).*
- An **nRF24L01+** or **nRF24L01+ PA+LNA** transceiver module powered by a clean, dedicated 3.3V regulator.

### Build & Flash
1. Clone the repository:
   ```bash
   git clone https://github.com/usercode-admin/JamLowEg.git
   cd JamLowEg
   ```
2. *(Optional for Arduino IDE)* Add the custom optimization flags above to your ESP32 core's `platform.local.txt` file to override the default `-Os` flag.
3. Review pin assignments and the `SWEEP_FULL` macro in `src/main.cpp`.
4. Compile and flash using PlatformIO:
   ```bash
   pio run --target upload
   ```

---

## Repository Structure

```text
JamLowEg/
├── .bin/           # Pre-compiled firmware binaries
├── csv/            # Partition tables and RF sweep benchmark logs
├── src/            # Core C++ firmware (register-level SPI & CW routines)
├── LICENSE         # MIT License
└── README.md       # Project documentation
```

---

## License

Distributed under the **MIT License**. See `LICENSE` for full terms and conditions.
