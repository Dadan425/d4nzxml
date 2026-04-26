/*
 * ================================================================
 *  PS4 Syscon UART Reader — Raspberry Pi Pico (C / Pico SDK)
 *  By: D4nzxml / Jonggol Game Center
 * ================================================================
 *  Pinout:
 *    GP0 (Pin 1)   = UART0 TX  →  Syscon RX
 *    GP1 (Pin 2)   = UART0 RX  ←  Syscon TX
 *    GND (Pin 3)   = GND       →  Syscon GND
 *    GP25 (onboard)= LED indikator
 *
 *  Di PC muncul sebagai COM port (USB CDC)
 *  Baud PC: 115200 (atau berapapun, USB CDC)
 *  Baud Syscon: 115200
 * ================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"

/* ── Konfigurasi ─────────────────────────────────────────── */
#define SYSCON_UART       uart0
#define SYSCON_BAUD       115200
#define UART_TX_PIN       0     /* GP0 */
#define UART_RX_PIN       1     /* GP1 */
#define LED_PIN           25    /* onboard LED */

#define RX_BUF_SIZE       4096
#define CMD_BUF_SIZE      256
#define RECV_TIMEOUT_MS   3000

/* ── Helpers: LED ─────────────────────────────────────────── */
static void led_blink(int n, int delay_ms) {
    for (int i = 0; i < n; i++) {
        gpio_put(LED_PIN, 1); sleep_ms(delay_ms);
        gpio_put(LED_PIN, 0); sleep_ms(delay_ms);
    }
}

/* ── UART Syscon ──────────────────────────────────────────── */
static void uart_flush_rx(void) {
    while (uart_is_readable(SYSCON_UART))
        uart_getc(SYSCON_UART);
}

static void syscon_send(const uint8_t *data, size_t len) {
    gpio_put(LED_PIN, 1);
    uart_write_blocking(SYSCON_UART, data, len);
    gpio_put(LED_PIN, 0);
}

/*
 * Terima data dari Syscon sampai timeout.
 * Return: jumlah byte diterima.
 */
static size_t syscon_recv(uint8_t *buf, size_t maxlen, uint32_t timeout_ms) {
    size_t   n     = 0;
    uint64_t start = time_us_64();
    uint64_t limit = timeout_ms * 1000ULL;
    uint64_t last  = start;

    while (n < maxlen) {
        if (uart_is_readable(SYSCON_UART)) {
            buf[n++] = uart_getc(SYSCON_UART);
            last     = time_us_64();
        } else {
            uint64_t now = time_us_64();
            /* Stop kalau sudah idle 300ms setelah byte terakhir */
            if (n > 0 && (now - last) > 300000ULL) break;
            /* Stop kalau total timeout */
            if ((now - start) > limit) break;
        }
    }
    return n;
}

/* ── Packet Builder ───────────────────────────────────────── */
/*
 * PS4 Syscon packet format (dari PS4SysconTools):
 *   [0xAA] [CMD_HI] [CMD_LO] [LEN] [DATA...] [XOR_CHECKSUM]
 *   Checksum = XOR semua byte setelah 0xAA
 */
static size_t build_packet(uint8_t cmd_hi, uint8_t cmd_lo,
                           const uint8_t *data, uint8_t data_len,
                           uint8_t *out) {
    out[0] = 0xAA;
    out[1] = cmd_hi;
    out[2] = cmd_lo;
    out[3] = data_len;
    if (data_len > 0 && data)
        memcpy(out + 4, data, data_len);

    uint8_t chk = 0;
    for (size_t i = 1; i < 4 + data_len; i++)
        chk ^= out[i];
    out[4 + data_len] = chk;

    return 5 + data_len;
}

/* ── Hex Dump ─────────────────────────────────────────────── */
static void hex_dump(const uint8_t *buf, size_t len) {
    if (len == 0) { printf("  (tidak ada data)\n"); return; }
    for (size_t i = 0; i < len; i += 16) {
        printf("  %08X  ", (unsigned)i);
        for (size_t j = 0; j < 16; j++) {
            if (i + j < len) printf("%02X ", buf[i + j]);
            else              printf("   ");
            if (j == 7)       printf(" ");
        }
        printf("  ");
        for (size_t j = 0; j < 16 && i + j < len; j++) {
            uint8_t c = buf[i + j];
            printf("%c", (c >= 32 && c < 127) ? c : '.');
        }
        printf("\n");
    }
}

/* ── Send Command & Print Response ───────────────────────── */
static size_t run_cmd(uint8_t cmd_hi, uint8_t cmd_lo,
                      const uint8_t *data, uint8_t dlen,
                      uint8_t *resp_buf, const char *label) {
    uint8_t pkt[64];
    size_t  pkt_len = build_packet(cmd_hi, cmd_lo, data, dlen, pkt);

    if (label) {
        printf("  >> Kirim %s: ", label);
        for (size_t i = 0; i < pkt_len; i++) printf("%02X ", pkt[i]);
        printf("\n");
    }

    uart_flush_rx();
    syscon_send(pkt, pkt_len);
    return syscon_recv(resp_buf, RX_BUF_SIZE, RECV_TIMEOUT_MS);
}

/* ── Perintah ─────────────────────────────────────────────── */
static uint8_t resp[RX_BUF_SIZE];

static void cmd_ping(void) {
    printf("[PING] Mengirim ping ke Syscon...\n");
    size_t n = run_cmd(0x00, 0x00, NULL, 0, resp, "PING");
    if (n) { printf("[PING] Response (%u bytes):\n", (unsigned)n); hex_dump(resp, n); }
    else     printf("[PING] Tidak ada response — cek kabel/koneksi\n");
}

static void cmd_version(void) {
    printf("[VERSION] Membaca versi Syscon...\n");
    size_t n = run_cmd(0x00, 0x01, NULL, 0, resp, "GET_VERSION");
    if (n) {
        printf("[VERSION] Raw (%u bytes):\n", (unsigned)n);
        hex_dump(resp, n);
        /* Coba print ASCII */
        printf("[VERSION] ASCII: ");
        for (size_t i = 0; i < n; i++)
            printf("%c", (resp[i] >= 32 && resp[i] < 127) ? resp[i] : '.');
        printf("\n");
    } else printf("[VERSION] Tidak ada response\n");
}

static void cmd_serial(void) {
    printf("[SERIAL] Membaca Serial Number...\n");
    size_t n = run_cmd(0x00, 0x04, NULL, 0, resp, "GET_SERIAL");
    if (n) {
        printf("[SERIAL] Raw (%u bytes):\n", (unsigned)n);
        hex_dump(resp, n);
        if (n > 4) {
            printf("[SERIAL] SN: ");
            for (size_t i = 4; i < n - 1 && i < 20; i++)
                printf("%c", (resp[i] >= 32 && resp[i] < 127) ? resp[i] : '.');
            printf("\n");
        }
    } else printf("[SERIAL] Tidak ada response\n");
}

static void cmd_errlog(void) {
    printf("[ERRLOG] Membaca error log...\n");
    size_t n = run_cmd(0x01, 0x00, NULL, 0, resp, "GET_ERRLOG");
    if (n) {
        printf("[ERRLOG] Raw (%u bytes):\n", (unsigned)n);
        hex_dump(resp, n);
        printf("[ERRLOG] Error Codes:\n");
        for (size_t i = 4; i + 4 <= n - 1; i += 4) {
            uint32_t code = ((uint32_t)resp[i]   << 24) |
                            ((uint32_t)resp[i+1] << 16) |
                            ((uint32_t)resp[i+2] <<  8) |
                             (uint32_t)resp[i+3];
            if (code != 0xFFFFFFFF && code != 0x00000000)
                printf("  → 0x%08X\n", code);
        }
    } else printf("[ERRLOG] Tidak ada response\n");
}

static void cmd_powerlog(void) {
    printf("[POWERLOG] Membaca power log...\n");
    size_t n = run_cmd(0x01, 0x01, NULL, 0, resp, "GET_POWERLOG");
    if (n) { printf("[POWERLOG] Raw (%u bytes):\n", (unsigned)n); hex_dump(resp, n); }
    else     printf("[POWERLOG] Tidak ada response\n");
}

static void cmd_nvram(void) {
    printf("[NVRAM] Dump NVRAM Syscon...\n");
    uint8_t all[4096]; size_t total = 0;
    for (int block = 0; block < 64 && total < sizeof(all); block++) {
        uint8_t arg = (uint8_t)block;
        size_t n = run_cmd(0x02, 0x00, &arg, 1, resp, NULL);
        if (n > 5) {
            size_t payload = n - 5;
            if (total + payload > sizeof(all)) payload = sizeof(all) - total;
            memcpy(all + total, resp + 4, payload);
            total += payload;
        }
        sleep_ms(20);
    }
    printf("[NVRAM] Total: %u bytes\n", (unsigned)total);
    hex_dump(all, total);
}

static void cmd_temp(void) {
    printf("[TEMP] Membaca suhu Syscon...\n");
    size_t n = run_cmd(0x03, 0x00, NULL, 0, resp, "GET_TEMP");
    if (n >= 6) {
        printf("[TEMP] Raw: ");
        for (size_t i = 0; i < n; i++) printf("%02X ", resp[i]);
        printf("\n");
        uint16_t raw = ((uint16_t)resp[4] << 8) | resp[5];
        printf("[TEMP] Suhu: %.1f C\n", raw * 0.0625f);
    } else printf("[TEMP] Tidak ada response atau format tidak dikenal\n");
}

static void cmd_status(void) {
    printf("[STATUS] Membaca status flags...\n");
    size_t n = run_cmd(0x00, 0x02, NULL, 0, resp, "GET_STATUS");
    if (n) { printf("[STATUS] Raw (%u bytes):\n", (unsigned)n); hex_dump(resp, n); }
    else     printf("[STATUS] Tidak ada response\n");
}

static void cmd_send_raw(const char *args) {
    uint8_t  raw[64];
    size_t   rawlen = 0;
    char     tmp[CMD_BUF_SIZE];
    strncpy(tmp, args, CMD_BUF_SIZE - 1);
    tmp[CMD_BUF_SIZE - 1] = '\0';

    char *tok = strtok(tmp, " \t");
    while (tok && rawlen < 64) {
        raw[rawlen++] = (uint8_t)strtol(tok, NULL, 16);
        tok = strtok(NULL, " \t");
    }
    if (rawlen == 0) { printf("[SEND] Format: SEND AA BB CC (hex)\n"); return; }

    printf("[SEND] Kirim: ");
    for (size_t i = 0; i < rawlen; i++) printf("%02X ", raw[i]);
    printf("\n");

    uart_flush_rx();
    syscon_send(raw, rawlen);
    size_t n = syscon_recv(resp, RX_BUF_SIZE, RECV_TIMEOUT_MS);
    if (n) { printf("[SEND] Response (%u bytes):\n", (unsigned)n); hex_dump(resp, n); }
    else     printf("[SEND] Tidak ada response\n");
}

static void cmd_bridge(void) {
    printf("[BRIDGE] Passthrough aktif. Kirim 'EXIT' + Enter untuk keluar.\n");
    uart_flush_rx();
    char line[CMD_BUF_SIZE];
    int  pos = 0;

    while (true) {
        /* PC → Syscon */
        int c = getchar_timeout_us(0);
        if (c != PICO_ERROR_TIMEOUT) {
            if (c == '\r' || c == '\n') {
                line[pos] = '\0';
                if (strcasecmp(line, "EXIT") == 0) break;
                /* Kirim sebagai hex bytes */
                char *tok = strtok(line, " \t");
                while (tok) {
                    uint8_t b = (uint8_t)strtol(tok, NULL, 16);
                    uart_write_blocking(SYSCON_UART, &b, 1);
                    tok = strtok(NULL, " \t");
                }
                pos = 0;
            } else if (pos < CMD_BUF_SIZE - 1) {
                line[pos++] = (char)c;
            }
        }
        /* Syscon → PC */
        if (uart_is_readable(SYSCON_UART)) {
            uint8_t b = uart_getc(SYSCON_UART);
            printf("%02X ", b);
            fflush(stdout);
        }
    }
    printf("\n[BRIDGE] Selesai.\n");
}

/* ── Banner & Help ─────────────────────────────────────────── */
static void print_banner(void) {
    printf("\n");
    printf("╔══════════════════════════════════════════════╗\n");
    printf("║  PS4 Syscon UART Reader v1.0 - JGC D4nzxml  ║\n");
    printf("╠══════════════════════════════════════════════╣\n");
    printf("║  GP0=TX(→SysconRX)  GP1=RX(←SysconTX)      ║\n");
    printf("║  Ketik HELP untuk daftar perintah            ║\n");
    printf("╚══════════════════════════════════════════════╝\n\n");
}

static void print_help(void) {
    printf("┌─── PERINTAH ────────────────────────────────────────┐\n");
    printf("│ HELP          - Tampilkan bantuan ini               │\n");
    printf("│ PING          - Test koneksi Syscon                 │\n");
    printf("│ VERSION       - Versi firmware Syscon               │\n");
    printf("│ SERIAL        - Serial number board                 │\n");
    printf("│ ERRLOG        - Error log Syscon                    │\n");
    printf("│ POWERLOG      - Power on/off log                    │\n");
    printf("│ NVRAM         - Dump NVRAM (raw hex)                │\n");
    printf("│ TEMP          - Suhu sensor Syscon                  │\n");
    printf("│ STATUS        - Status flags Syscon                 │\n");
    printf("│ SEND AA BB .. - Kirim raw bytes hex ke Syscon       │\n");
    printf("│ BRIDGE        - Passthrough PC <-> Syscon (hex)     │\n");
    printf("│ FLUSH         - Bersihkan buffer UART               │\n");
    printf("└─────────────────────────────────────────────────────┘\n\n");
}

/* ── Command Parser ───────────────────────────────────────── */
static void process_cmd(char *line) {
    /* Trim trailing whitespace */
    size_t len = strlen(line);
    while (len > 0 && (line[len-1] == '\r' || line[len-1] == '\n' ||
                        line[len-1] == ' '))
        line[--len] = '\0';
    if (len == 0) return;

    /* Uppercase untuk compare */
    char upper[CMD_BUF_SIZE];
    strncpy(upper, line, CMD_BUF_SIZE - 1);
    upper[CMD_BUF_SIZE - 1] = '\0';
    for (size_t i = 0; i < strlen(upper); i++)
        upper[i] = (char)toupper((unsigned char)upper[i]);

    if      (strcmp(upper, "HELP")     == 0) print_help();
    else if (strcmp(upper, "PING")     == 0) cmd_ping();
    else if (strcmp(upper, "VERSION")  == 0) cmd_version();
    else if (strcmp(upper, "SERIAL")   == 0) cmd_serial();
    else if (strcmp(upper, "ERRLOG")   == 0) cmd_errlog();
    else if (strcmp(upper, "POWERLOG") == 0) cmd_powerlog();
    else if (strcmp(upper, "NVRAM")    == 0) cmd_nvram();
    else if (strcmp(upper, "TEMP")     == 0) cmd_temp();
    else if (strcmp(upper, "STATUS")   == 0) cmd_status();
    else if (strcmp(upper, "FLUSH")    == 0) { uart_flush_rx(); printf("[FLUSH] Buffer bersih\n"); }
    else if (strncmp(upper, "SEND ", 5) == 0) cmd_send_raw(line + 5);
    else if (strcmp(upper, "BRIDGE")   == 0) cmd_bridge();
    else printf("[ERR] Perintah tidak dikenal: '%s' — ketik HELP\n", line);
}

/* ── Main ─────────────────────────────────────────────────── */
int main(void) {
    /* Init stdio USB (muncul sebagai COM port di PC) */
    stdio_usb_init();

    /* Init UART0 ke Syscon */
    uart_init(SYSCON_UART, SYSCON_BAUD);
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
    uart_set_hw_flow(SYSCON_UART, false, false);
    uart_set_format(SYSCON_UART, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(SYSCON_UART, true);

    /* Init LED */
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    /* Tunggu USB host connect */
    led_blink(5, 100);
    sleep_ms(1000);

    print_banner();

    /* Main loop */
    char line[CMD_BUF_SIZE];
    int  pos = 0;

    while (true) {
        printf("JGC-SYSCON> ");
        fflush(stdout);
        pos = 0;

        while (true) {
            int c = getchar();
            if (c == '\r' || c == '\n') {
                line[pos] = '\0';
                printf("\n");
                break;
            } else if (c == 127 || c == '\b') {
                /* Backspace */
                if (pos > 0) { pos--; printf("\b \b"); fflush(stdout); }
            } else if (c >= 32 && pos < CMD_BUF_SIZE - 1) {
                line[pos++] = (char)c;
                printf("%c", c);
                fflush(stdout);
            }
        }
        process_cmd(line);
    }

    return 0;
}
