#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "sha256.h"

#define PUF_SIZE 1024

/* Address definitions matching shifted LinkerScript.ld memory regions */
#define PUF_RAM_BASE   0x20008000UL
#define TRNG_RAM_BASE  0x20008400UL

/* Hardware Register Definitions */
#define RCC_AHB1ENR    (*(volatile uint32_t *)0x40023830UL)
#define RCC_APB1ENR    (*(volatile uint32_t *)0x40023840UL)

#define GPIOA_MODER    (*(volatile uint32_t *)0x40020000UL)
#define GPIOA_ODR      (*(volatile uint32_t *)0x40020014UL)
#define GPIOA_AFRL     (*(volatile uint32_t *)0x40020020UL)

#define USART2_SR      (*(volatile uint32_t *)0x40004400UL)
#define USART2_DR      (*(volatile uint32_t *)0x40004404UL)
#define USART2_BRR     (*(volatile uint32_t *)0x40004408UL)
#define USART2_CR1     (*(volatile uint32_t *)0x4000440CUL)

#define FLASH_KEYR     (*(volatile uint32_t *)0x40023C04UL)
#define FLASH_SR       (*(volatile uint32_t *)0x40023C0CUL)
#define FLASH_CR       (*(volatile uint32_t *)0x40023C10UL)

#define FLASH_SECTOR_5 0x08020000UL
#define MAGIC_ENROLLED 0xA5A59999UL

typedef struct {
    uint32_t magic;
    uint8_t enrollment_sram[32]; // Enrolled SRAM baseline
    uint8_t helper[32];
    uint8_t digest[32];
} puf_store_t;

#define STORE ((const puf_store_t *)FLASH_SECTOR_5)

static uint8_t puf_raw[PUF_SIZE];
static uint8_t key_buf[32];

/* --- Low-level UART Helpers --- */

static void uart2_init(void) {
    RCC_AHB1ENR |= (1 << 0);
    RCC_APB1ENR |= (1 << 17);

    // PA2 -> USART2 TX
    GPIOA_MODER &= ~(3 << (2 * 2));
    GPIOA_MODER |=  (2 << (2 * 2));
    GPIOA_AFRL  &= ~(0xF << (2 * 4));
    GPIOA_AFRL  |=  (0x7 << (2 * 4));

    USART2_BRR = 0x8B; // 115200 @ 16MHz
    USART2_CR1 = (1 << 3) | (1 << 13);
}

static void uart_putc(char c) {
    while (!(USART2_SR & (1 << 7)));
    USART2_DR = (uint8_t)c;
}

static void uart_puts(const char *s) {
    while (*s) uart_putc(*s++);
}

static void uart_print_num(uint32_t num) {
    char buf[11];
    int i = 10;
    buf[i] = '\0';
    if (num == 0) {
        uart_putc('0');
        return;
    }
    while (num > 0 && i > 0) {
        buf[--i] = '0' + (num % 10);
        num /= 10;
    }
    uart_puts(&buf[i]);
}

static void uart_hex_dump(const uint8_t *buf, uint32_t len) {
    const char hex[] = "0123456789ABCDEF";
    for (uint32_t i = 0; i < len; i++) {
        uart_putc(hex[(buf[i] >> 4) & 0x0F]);
        uart_putc(hex[buf[i] & 0x0F]);
    }
    uart_puts("\r\n");
}

/* --- Fixed 32-bit Flash Driver --- */

static void flash_clear_flags(void) {
    FLASH_SR = 0xF3;
}

static void flash_unlock(void) {
    if (FLASH_CR & (1U << 31)) {
        FLASH_KEYR = 0x45670123;
        FLASH_KEYR = 0xCDEF89AB;
    }
}

static void flash_lock(void) {
    FLASH_CR |= (1U << 31);
}

static void flash_erase_s5(void) {
    while (FLASH_SR & (1U << 16));
    flash_unlock();
    flash_clear_flags();

    FLASH_CR &= ~(0xFU << 3);
    FLASH_CR |=  (5U << 3);
    FLASH_CR |=  (1U << 1);
    FLASH_CR |=  (1U << 16);

    while (FLASH_SR & (1U << 16));
    FLASH_CR &= ~(1U << 1);
    flash_lock();
}

static void flash_program_words(uint32_t addr, const uint32_t *src, uint32_t word_count) {
    flash_unlock();
    flash_clear_flags();

    FLASH_CR &= ~(3U << 8);
    FLASH_CR |=  (2U << 8);

    for (uint32_t i = 0; i < word_count; i++) {
        while (FLASH_SR & (1U << 16));

        FLASH_CR |= (1U << 0);
        *(volatile uint32_t *)(addr + (i * 4)) = src[i];

        while (FLASH_SR & (1U << 16));
    }

    FLASH_CR &= ~(1U << 0);
    flash_lock();
}

/* --- Crypto Helpers --- */

static void hash_buffer(const uint8_t *in, uint32_t len, uint8_t *out) {
    SHA256_CTX ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, in, len);
    sha256_final(&ctx, out);
}

/* --- Application Entry --- */

int main(void) {
    // 1. Direct Physical Memory Capture from offset 0x20008000
    volatile const uint8_t *raw_puf_hw_ptr = (volatile const uint8_t *)PUF_RAM_BASE;
    for (int i = 0; i < PUF_SIZE; i++) {
        puf_raw[i] = raw_puf_hw_ptr[i];
    }

    // Setup User LED (PA5)
    RCC_AHB1ENR |= (1 << 0);
    GPIOA_MODER &= ~(3 << (5 * 2));
    GPIOA_MODER |=  (1 << (5 * 2));
    GPIOA_ODR   |=  (1 << 5);

    uart2_init();

    // Small delay to allow terminal connection after reboot
    for (volatile int i = 0; i < 8000000; i++);

    uart_puts("\r\n========================================\r\n");
    uart_puts("   STM32F4 PUF ZERO-TRUST PIPELINE     \r\n");
    uart_puts("   (Mapped to Address: 0x20008000)     \r\n");
    uart_puts("========================================\r\n");

    uart_puts("[DEBUG] Raw First 32 Bytes at Boot:\r\n");
    uart_hex_dump(puf_raw, 32);

    /* --------------------------------------------------------
     * ENROLLMENT PHASE
     * -------------------------------------------------------- */
    if (STORE->magic != MAGIC_ENROLLED) {
        uart_puts("[PUF] Unenrolled device. Starting factory setup...\r\n");

        volatile const uint8_t *raw_trng_hw_ptr = (volatile const uint8_t *)TRNG_RAM_BASE;
        hash_buffer((const uint8_t *)raw_trng_hw_ptr, PUF_SIZE, key_buf);

        puf_store_t local_store;
        local_store.magic = MAGIC_ENROLLED;

        // Save raw enrolled baseline from 0x20008000
        memcpy(local_store.enrollment_sram, puf_raw, 32);

        // Compute helper data W = R_enroll ^ Key
        for (int i = 0; i < 32; i++) {
            local_store.helper[i] = puf_raw[i] ^ key_buf[i];
        }

        hash_buffer(key_buf, 32, local_store.digest);

        flash_erase_s5();

        uint32_t words_to_write = (sizeof(puf_store_t) + 3) / 4;
        flash_program_words(FLASH_SECTOR_5, (const uint32_t *)&local_store, words_to_write);

        uart_puts("[PUF] Factory Enrollment Completed at 0x20008000.\r\n");
        uart_puts("[ACTION] Erase Flash to re-enroll, or power cycle board to test cold boot BER.\r\n");

        while (1) {
            for (volatile int i = 0; i < 100000; i++);
            GPIOA_ODR ^= (1 << 5);
        }
    }

    /* --------------------------------------------------------
     * RECONSTRUCTION & BIT FLIP ANALYSIS PHASE
     * -------------------------------------------------------- */
    uart_puts("[PUF] Enrolled baseline found. Evaluating startup noise...\r\n");

    uint32_t bit_errors = 0;
    for (int i = 0; i < 32; i++) {
        uint8_t diff = puf_raw[i] ^ STORE->enrollment_sram[i];
        while (diff) {
            bit_errors += (diff & 1);
            diff >>= 1;
        }
    }

    uint32_t ber_pct_whole = (bit_errors * 100) / 256;
    uint32_t ber_pct_frac  = ((bit_errors * 10000) / 256) % 100;

    uart_puts("[PUF] --- PHYSICAL NOISE REPORT ---\r\n");
    uart_puts("[PUF] Enrolled Bits Evaluated : 256 bits\r\n");
    uart_puts("[PUF] Flipped Bits Detected   : ");
    uart_print_num(bit_errors);
    uart_puts(" / 256 bits\r\n");

    uart_puts("[PUF] Bit Error Rate (BER)    : ");
    uart_print_num(ber_pct_whole);
    uart_putc('.');
    if (ber_pct_frac < 10) uart_putc('0');
    uart_print_num(ber_pct_frac);
    uart_puts("%\r\n");
    uart_puts("-----------------------------------\r\n");

    // Key Reconstruction
    uint8_t key_reconstructed[32];
    uint8_t hash_reconstructed[32];

    for (int i = 0; i < 32; i++) {
        key_reconstructed[i] = puf_raw[i] ^ STORE->helper[i];
    }

    hash_buffer(key_reconstructed, 32, hash_reconstructed);

    if (memcmp(hash_reconstructed, STORE->digest, 32) == 0) {
        uart_puts("[RESULT] KEY MATCHED EXACTLY! (0 Bit Flips / Warm Reset)\r\n");
    } else {
        uart_puts("[RESULT] UNCORRECTED BER MISMATCH (Requires Fuzzy Extractor / ECC)\r\n");
        memset(key_reconstructed, 0, sizeof(key_reconstructed));
    }

    GPIOA_ODR |= (1 << 5);

    while (1) {
        for (volatile int i = 0; i < 1000000; i++);
        GPIOA_ODR ^= (1 << 5);
    }
}
