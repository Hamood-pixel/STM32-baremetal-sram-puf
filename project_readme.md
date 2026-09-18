# STM32 Bare-Metal SRAM Physical Unclonable Function (PUF)

A bare-metal C implementation of an SRAM-based **Physical Unclonable Function (PUF)** running on the **STM32F401RE Nucleo-64** microcontroller. This project extracts intrinsic hardware entropy from SRAM power-up states to generate device-unique cryptographic keys and perform cold-boot identity verification.

---

## 📌 Features

- **Direct Physical SRAM Capture:** Reads raw startup entropy from dedicated SRAM hardware memory regions prior to stack initialization or standard C runtime memory clears (`.bss`).
- **Memory Collision Avoidance:** Re-allocates PUF buffer arrays to safe RAM addresses (`0x20008000`) using custom `NOLOAD` linker script sections to prevent startup vector and Main Stack Pointer (`_estack`) overwrites.
- **Flash Sector Enrollment:** Stores enrollment baselines and helper data directly inside **Flash Sector 5** (`0x08020000`) using low-level flash driver operations.
- **Bit Error Rate (BER) Diagnostics:** Computes intra-device Hamming distance and reports real-time Bit Error Rate (BER) statistics over UART.
- **On-Board SHA-256 Integration:** Uses custom software SHA-256 hashing for key verification and entropy conditioning.

---

## 🛠️ Hardware & Tools

| Component | Description |
| :--- | :--- |
| **Microcontroller** | STMicroelectronics STM32F401RE (ARM Cortex-M4) |
| **Development Board**| NUCLEO-F401RE |
| **Toolchain** | STM32CubeIDE (GCC ARM Embedded) |
| **Interface** | USART2 (TX: PA2, RX: PA3) @ 115200 Baud |

---

## 🧠 Technical Overview & Architecture

### Memory Layout (`LinkerScript.ld`)

To isolate raw SRAM cells from hardware stack initialization, custom memory regions are declared in the linker script:

```ld
MEMORY
{
  RAM       (xrw) : ORIGIN = 0x20000000, LENGTH = 32K  /* General SRAM & Main Stack */
  PUF_RAM   (xrw) : ORIGIN = 0x20008000, LENGTH = 1K   /* PUF Startup Entropy Region */
  TRNG_RAM  (xrw) : ORIGIN = 0x20008400, LENGTH = 1K   /* TRNG Entropy Buffer */
  RAM_UPPER (xrw) : ORIGIN = 0x20008800, LENGTH = 62K  /* Remaining Application RAM */
  FLASH      (rx) : ORIGIN = 0x08000000, LENGTH = 512K /* System Flash */
}

SECTIONS
{
  .puf_data (NOLOAD) :
  {
    . = ALIGN(4);
    *(.puf_data)
    . = ALIGN(4);
  } > PUF_RAM
  
  .trng_data (NOLOAD) :
  {
    . = ALIGN(4);
    *(.trng_data)
    . = ALIGN(4);
  } > TRNG_RAM
  
  /* Standard Flash/RAM sections follow... */
}
```

### Zero-Trust PUF Protocol Lifecycle

1. **Power-On Reset:** Immediate memory snapshot captured from physical address `0x20008000`.
2. **Enrollment Check:** Checks Flash Sector 5 (`0x08020000`) for the `MAGIC_ENROLLED` token.
   - **If Unenrolled:** Performs factory setup by hashing entropy, calculating XOR helper data ($W = R_{enroll} \oplus K$), and storing baseline parameters to Flash.
   - **If Enrolled:** Compares current raw SRAM data against stored baseline data to compute flipped bit count and BER percentage.
3. **Key Reconstruction:** XOR-reconstructs candidate key bytes using current SRAM values and saved helper data, validating against stored SHA-256 digests.

---

## 🚀 Getting Started

### 1. Prerequisites
- [STM32CubeIDE](https://www.st.com/en/development-tools/stm32cubeide.html) (v1.10.0 or higher)
- Serial Terminal software (PuTTY, Tera Term, or Serial Monitor)

### 2. Building and Flashing
1. Clone this repository:
   ```bash
   git clone https://github.com/Hamood-pixel/STM32-baremetal-sram-puf.git
   ```
2. Open STM32CubeIDE and import the project:
   `File` -> `Import...` -> `General` -> `Existing Projects into Workspace`.
3. Select the project root folder and click **Finish**.
4. Build the project (`Ctrl + B`).
5. Flash the target MCU using ST-LINK.

### 3. Execution & Testing Protocol

To verify cold-boot hardware entropy and Bit Error Rate (BER):

1. **Connect Serial Terminal:** Open PuTTY on the ST-LINK Virtual COM Port:
   - **Baud Rate:** `115200`
   - **Data Bits:** `8`, **Stop Bits:** `1`, **Parity:** `None`
2. **Factory Enrollment:**
   - On first run (or after erasing Flash Sector 5), the terminal will report:
     ```text
     [PUF] Unenrolled device. Starting factory setup...
     [PUF] Factory Enrollment Completed at 0x20008000.
     ```
3. **Cold Boot Test Procedure:**
   - Disconnect the USB cable completely from the Nucleo board.
   - **Wait 10 seconds** to allow SRAM power degradation/discharge.
   - Reconnect the USB cable and observe UART diagnostics:
     ```text
     ========================================
        STM32F4 PUF ZERO-TRUST PIPELINE     
        (Mapped to Address: 0x20008000)     
     ========================================
     [DEBUG] Raw First 32 Bytes at Boot:
     51A8D3EABCAAECC4382735723A8097B783828F0390B36FBFBB6E90F661395BF7
     [PUF] Enrolled baseline found. Evaluating startup noise...
     [PUF] --- PHYSICAL NOISE REPORT ---
     [PUF] Enrolled Bits Evaluated : 256 bits
     [PUF] Flipped Bits Detected   : 14 / 256 bits
     [PUF] Bit Error Rate (BER)    : 5.46%
     -----------------------------------
     [RESULT] KEY MATCHED EXACTLY!
     ```

---

## 📝 License

Distributed under the MIT License. See `LICENSE` for more information.