# Bare-Metal SRAM Physical Unclonable Function (PUF) on STM32F401RE

An embedded C, bare-metal implementation of an **SRAM-based Physical Unclonable Function (PUF)** on an **STM32F401RE Nucleo-64** microcontroller. 

This project extracts the unique startup state of uninitialized SRAM cells to generate a hardware-intrinsic device fingerprint, utilizing **Flash Sector 5** for baseline enrollment and **SHA-256 cryptographic hashing** via Mbed TLS for key reconstruction.

---

## 📌 Project Overview

Due to subtle manufacturing variations in silicon, SRAM cells settle into unique, unpredictable patterns of `0`s and `1`s upon initial power-up. This project leverages these physical variations to turn an STM32 microcontroller's internal SRAM into a hardware root of trust.

### Key Features
* **Custom Linker Mapping:** Dedicated `PUF_RAM` section strictly allocated at `0x20008000` to isolate PUF target buffers from the stack, heap, and vector tables.
* **Enrollment & Reconstruction Pipeline:** Auto-detects factory state vs. enrolled state using a persistent magic marker (`MAGIC_ENROLLED`) stored in Flash Sector 5 (`0x08020000`).
* **Hamming Distance & Bit Error Rate (BER) Evaluation:** Calculates exact bit flip counts and percentage BER between live SRAM power-up states and the stored baseline.
* **Cryptographic Hash Key Generation:** Computes SHA-256 digests over candidate SRAM responses using Mbed TLS integration.
* **Serial Diagnostic Logging:** Outputs structured binary dumps, BER statistics, and SHA-256 fingerprints over USART2 (115200 baud).

---

## 🛠️ Hardware & Software Requirements

* **Development Board:** NUCLEO-F401RE (ARM Cortex-M4 @ 84 MHz)
* **IDE / Toolchain:** STM32CubeIDE / GNU Tools for STM32 (GCC cross-compiler)
* **Cryptographic Library:** Mbed TLS (SHA-256 modules)
* **Terminal Utility:** PuTTY, Tera Term, or Minicom (115200 8N1)

---

## 📐 Memory Map Architecture

| Memory Region | Start Address | Size | Description |
| :--- | :--- | :--- | :--- |
| **SRAM (System Stack/Heap)** | `0x20000000` | 32 KB | System execution RAM, MSP stack, vector table |
| **PUF_RAM** | `0x20008000` | 256 bytes | Uninitialized target SRAM region for fingerprinting |
| **TRNG_RAM** | `0x20008400` | 256 bytes | Auxiliary RAM buffer for entropy harvesting |
| **Flash Sector 5** | `0x08020000` | 128 KB | Non-volatile flash storage for enrollment baseline |

---

## 🚀 Cold-Boot Protocol & Testing

SRAM cells require complete charge dissipation across internal decoupling capacitors to return to their natural physical startup preference.

1. **Flashing Code:** Flash the firmware using STM32CubeIDE.
2. **First Power-Up (Factory Enrollment):**
   * The core checks Sector 5 for `MAGIC_ENROLLED`.
   * Finding an uninitialized state, it executes `flash_erase_s5()`, writes the `0x20008000` SRAM baseline to Flash, and stores the magic constant.
3. **Cold-Boot Testing (BER Verification):**
   * Disconnect power (USB cable) for **10 seconds** to allow full power-rail discharge.
   * Reconnect power and open the serial terminal at **115200 baud**.
   * The firmware will measure bit flips between the live SRAM state and the Flash baseline.

---

## 📜 License

Distributed under the MIT License. See `LICENSE` for more information.
