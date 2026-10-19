/*
 * Ung dung node:
 *   - Doc 3 dau vao opto, 2 cam bien luu luong, thanh ghi Modbus qua RS485,
 *     trang thai sac/pin tu BQ25798
 *   - Gui dinh ky ve gateway qua LoRa (goi chu JSON, gateway chuyen tiep len MQTT)
 *   - Nhan lenh tu gateway qua LoRa: "@<ma node> <lenh>" hoac "@* <lenh>" (moi node)
 *   - Hien thi trang thai len TFT
 *   - Console tren USART1 (CN6) de thu tung khoi, go "help"
 *
 * Tu gateway thu: lora @1A2B3C4D relay 1 1
 */
#include "app.h"
#include "board.h"
#include "bq25798.h"
#include "clock.h"
#include "config.h"
#include "modbus.h"
#include "serial.h"
#include "sx1262.h"
#include "tft.h"
#include "w25q.h"
#include "main.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

extern I2C_HandleTypeDef hi2c1;
extern I2C_HandleTypeDef hi2c2;

#define LORA_MB_MAX     16u     /* so thanh ghi Modbus toi da dua vao goi LoRa */

static bool lora_ok, flash_ok, tft_ok;
static char node_id[9];

static mb_status_t mb_st = MB_ERR_TIMEOUT;
static uint16_t mb_regs[MB_MAX_REGS];

static bq_status_t bq;
static bool bq_ok;

static uint32_t lora_tx_count, lora_tx_fail, lora_rx_count, lora_cmd_count;
static uint16_t lora_seq;
static int16_t lora_last_rssi;
static int8_t lora_last_snr;

static uint32_t t_1s, t_mb, t_led3;
static uint32_t t_last_tx;          /* lan gui gan nhat */
static uint32_t t_next_tx;          /* lan gui ke tiep da hen */
static bool report_now;

static char con_buf[128];
static uint8_t con_len;

/* ---------------------------------------------------------------- */

static uint32_t rand_state;

static uint32_t rnd(uint32_t max)
{
    rand_state = rand_state * 1664525u + 1013904223u;
    return max ? (rand_state >> 8) % (max + 1u) : 0;
}

static void make_node_id(void)
{
    uint32_t id = CFG_NODE_ID;
    if (id == 0) {
        /* FNV-1a tren 96 bit UID */
        uint32_t w[3] = { HAL_GetUIDw0(), HAL_GetUIDw1(), HAL_GetUIDw2() };
        const uint8_t *p = (const uint8_t *)w;
        id = 2166136261u;
        for (int i = 0; i < 12; i++) { id = (id ^ p[i]) * 16777619u; }
    }
    snprintf(node_id, sizeof(node_id), "%08lX", (unsigned long)id);
    rand_state = id ^ HAL_GetTick();
}

/* Hen lan gui ke tiep: binh thuong = chu ky + tre ngau nhien */
static void schedule_periodic(void)
{
    t_next_tx = t_last_tx + CFG_REPORT_PERIOD_MS + rnd(CFG_REPORT_JITTER_MS);
}

/* Gui som (dau vao doi / co lenh): cho 200..700 ms de gateway kip quay ve RX,
 * va khong gan lan gui truoc hon CFG_REPORT_MIN_GAP_MS */
static void schedule_soon(void)
{
    uint32_t now = HAL_GetTick();
    uint32_t t = now + 200u + rnd(500u);
    if ((int32_t)(t_last_tx + CFG_REPORT_MIN_GAP_MS - t) > 0) { t = t_last_tx + CFG_REPORT_MIN_GAP_MS; }
    if ((int32_t)(t - t_next_tx) < 0) { t_next_tx = t; }
}

static void i2c_scan(I2C_HandleTypeDef *h, const char *name)
{
    int n = 0;
    printf("%s:", name);
    for (uint8_t a = 0x08; a < 0x78; a++) {
        if (HAL_I2C_IsDeviceReady(h, (uint16_t)(a << 1), 1, 2) == HAL_OK) {
            printf(" 0x%02X", a);
            n++;
        }
    }
    printf(n ? "\n" : " khong co thiet bi\n");
}

static void flash_test(void)
{
    if (!flash_ok) {
        printf("Flash: chua nhan chip (kiem tra day FLASH-CS -> PA4)\n");
        return;
    }
    uint32_t addr = w25q_size() - W25Q_SECTOR_SIZE;   /* sector cuoi */
    const char msg[] = "AVIET-NODE flash test";
    char back[sizeof(msg)] = { 0 };
    w25q_erase_sector(addr);
    w25q_write(addr, msg, sizeof(msg));
    w25q_read(addr, back, sizeof(back));
    printf("Flash ghi/doc sector 0x%06lX: %s\n", (unsigned long)addr,
           memcmp(msg, back, sizeof(msg)) == 0 ? "OK" : "SAI");
}

static void poll_modbus(void)
{
    mb_st = mb_read_regs(CFG_MB_SLAVE, CFG_MB_FUNC, CFG_MB_START_REG, CFG_MB_REG_COUNT, mb_regs);
}

static void poll_bq(void)
{
    bq_ok = bq_read(&bq);
}

static void print_status(void)
{
    printf("--- Node %s ---\n", node_id);
    printf("IN1..3 : %d %d %d\n", board_input_get(1), board_input_get(2), board_input_get(3));
    printf("RELAY  : %d %d\n", board_relay_get(1), board_relay_get(2));
    for (int i = 1; i <= 2; i++) {
        printf("FLOW%d  : %lu mL/phut, tong %lu mL (%lu xung)\n", i,
               (unsigned long)board_flow_ml_per_min(i), (unsigned long)board_flow_total_ml(i),
               (unsigned long)board_flow_pulses(i));
    }
    printf("Modbus : slave %d FC%d -> %s", CFG_MB_SLAVE, CFG_MB_FUNC, mb_status_str(mb_st));
    if (mb_st == MB_OK) {
        for (int i = 0; i < CFG_MB_REG_COUNT; i++) { printf(" %u", mb_regs[i]); }
    }
    printf("\n");
    if (bq_ok) {
        printf("Pin    : VBAT %u mV, IBAT %d mA, VIN %u mV, %s\n",
               bq.vbat_mv, bq.ibat_ma, bq.vbus_mv, bq_chg_str(bq.chg_stat));
    } else {
        printf("Pin    : BQ25798 khong tra loi\n");
    }
    printf("LoRa   : %s, gui %lu (loi %lu), nhan %lu (lenh %lu), RSSI cuoi %d dBm SNR %d\n",
           lora_ok ? "OK" : "LOI", (unsigned long)lora_tx_count, (unsigned long)lora_tx_fail,
           (unsigned long)lora_rx_count, (unsigned long)lora_cmd_count, lora_last_rssi, lora_last_snr);
    printf("Gui toi: sau %ld s\n", (long)((int32_t)(t_next_tx - HAL_GetTick()) / 1000));
    printf("Flash  : %s, TFT: %s\n", flash_ok ? "OK" : "LOI", tft_ok ? "da khoi tao" : "-");
    char ts[24];
    clk_format(ts, sizeof(ts));
    printf("Gio    : %s\n", ts[0] ? ts : "chua dat (gateway gui lenh time, hoac go tay)");
}

/*
 * Goi gui ve gateway, vi du:
 * {"id":"1A2B3C4D","n":12,"up":3600,"in":[0,1,0],"rl":[0,1],"f1":[1500,23000],"f2":[0,0],
 *  "vb":12450,"ib":-120,"vin":0,"chg":0,"rs":-87,"mb":[1,2,3]}
 *   f1/f2 = [mL/phut, tong mL]; vb/ib = pin mV/mA; vin = nguon vao mV; chg = trang thai sac;
 *   rs = RSSI goi cuoi nhan tu gateway; mb = thanh ghi Modbus hoac "mbe":"<loi>".
 */
static int build_report(char *buf, size_t len)
{
    int n = snprintf(buf, len,
        "{\"id\":\"%s\",\"n\":%u,\"up\":%lu,\"in\":[%d,%d,%d],\"rl\":[%d,%d],"
        "\"f1\":[%lu,%lu],\"f2\":[%lu,%lu]",
        node_id, lora_seq, (unsigned long)(HAL_GetTick() / 1000u),
        board_input_get(1), board_input_get(2), board_input_get(3),
        board_relay_get(1), board_relay_get(2),
        (unsigned long)board_flow_ml_per_min(1), (unsigned long)board_flow_total_ml(1),
        (unsigned long)board_flow_ml_per_min(2), (unsigned long)board_flow_total_ml(2));
    if (bq_ok) {
        n += snprintf(buf + n, len - (size_t)n, ",\"vb\":%u,\"ib\":%d,\"vin\":%u,\"chg\":%u",
                      bq.vbat_mv, bq.ibat_ma, bq.vbus_mv, bq.chg_stat);
    }
    if (lora_rx_count) {
        n += snprintf(buf + n, len - (size_t)n, ",\"rs\":%d", lora_last_rssi);
    }
    if (mb_st == MB_OK) {
        unsigned cnt = CFG_MB_REG_COUNT < LORA_MB_MAX ? CFG_MB_REG_COUNT : LORA_MB_MAX;
        n += snprintf(buf + n, len - (size_t)n, ",\"mb\":[");
        for (unsigned i = 0; i < cnt; i++) {
            n += snprintf(buf + n, len - (size_t)n, i ? ",%u" : "%u", mb_regs[i]);
        }
        n += snprintf(buf + n, len - (size_t)n, "]");
    } else {
        n += snprintf(buf + n, len - (size_t)n, ",\"mbe\":\"%s\"", mb_status_str(mb_st));
    }
    n += snprintf(buf + n, len - (size_t)n, "}");
    return n;
}

static void send_report(void)
{
    char buf[320];
    int n = build_report(buf, sizeof(buf));

    t_last_tx = HAL_GetTick();
    schedule_periodic();
    if (n > SX1262_MAX_PAYLOAD) {
        printf("[LORA] goi %d byte qua dai, giam CFG_MB_REG_COUNT\n", n);
        return;
    }
    printf("[LORA] gui %d byte: %s\n", n, buf);
    if (!lora_ok) { return; }

    board_led3_set(true);
    t_led3 = HAL_GetTick();
    bool ok = sx1262_send((const uint8_t *)buf, (uint8_t)n, 5000);
    lora_seq++;
    if (ok) {
        lora_tx_count++;
    } else {
        lora_tx_fail++;
        printf("[LORA] gui LOI\n");
    }
}

/* ---------------- lenh: tu console hoac tu gateway ---------------- */

static void cmd_exec(char *s, bool remote)
{
    char *argv[6];
    int argc = 0;

    for (char *tok = strtok(s, " "); tok && argc < 6; tok = strtok(NULL, " ")) {
        argv[argc++] = tok;
    }
    if (argc == 0) { return; }

    if (strcmp(argv[0], "relay") == 0 && argc == 3) {
        board_relay_set(atoi(argv[1]), atoi(argv[2]) != 0);
        printf("Relay%s = %d\n", argv[1], board_relay_get(atoi(argv[1])));
        report_now = true;
    } else if (strcmp(argv[0], "send") == 0 || strcmp(argv[0], "pub") == 0 ||
               strcmp(argv[0], "status") == 0) {
        if (remote) {
            report_now = true;
        } else if (strcmp(argv[0], "status") == 0) {
            print_status();
        } else {
            send_report();
        }
    } else if (strcmp(argv[0], "time") == 0) {
        char ts[24];
        if (argc == 3) {
            char both[24];
            clk_time_t t;
            snprintf(both, sizeof(both), "%s %s", argv[1], argv[2]);
            if (!clk_parse(both, &t) || !clk_set(&t)) {
                printf("Sai dinh dang: time YYYY-MM-DD HH:MM:SS\n");
                return;
            }
        }
        clk_format(ts, sizeof(ts));
        printf("Gio RTC: %s\n", ts[0] ? ts : "chua dat");
    } else if (strcmp(argv[0], "mbw") == 0 && argc == 4) {
        mb_status_t st = mb_write_reg((uint8_t)atoi(argv[1]), (uint16_t)atoi(argv[2]),
                                      (uint16_t)atoi(argv[3]));
        printf("Modbus ghi: %s\n", mb_status_str(st));
        poll_modbus();
        report_now = true;
    } else if (strcmp(argv[0], "chg") == 0 && argc == 2) {
        bool on = atoi(argv[1]) != 0;
        printf("Sac pin: %s -> %s\n", on ? "bat" : "tat", bq_charge_enable(on) ? "OK" : "LOI");
        report_now = true;
    } else if (remote) {
        printf("Lenh tu xa khong hop le: %s\n", argv[0]);
    } else if (strcmp(argv[0], "mb") == 0 && argc == 5) {
        uint16_t v[MB_MAX_REGS];
        uint16_t cnt = (uint16_t)atoi(argv[4]);
        mb_status_t st = mb_read_regs((uint8_t)atoi(argv[1]), (uint8_t)atoi(argv[2]),
                                      (uint16_t)atoi(argv[3]), cnt, v);
        printf("Modbus: %s", mb_status_str(st));
        if (st == MB_OK) { for (int i = 0; i < cnt; i++) { printf(" %u", v[i]); } }
        if (st == MB_ERR_EXCEPTION) { printf(" ma %u", mb_last_exception()); }
        printf("\n");
    } else if (strcmp(argv[0], "lora") == 0 && argc >= 2) {
        char msg[128] = "";
        for (int i = 1; i < argc; i++) {
            if (i > 1) { strncat(msg, " ", sizeof(msg) - strlen(msg) - 1); }
            strncat(msg, argv[i], sizeof(msg) - strlen(msg) - 1);
        }
        bool ok = sx1262_send((const uint8_t *)msg, (uint8_t)strlen(msg), 5000);
        printf("LoRa gui \"%s\": %s\n", msg, ok ? "OK" : "LOI");
    } else if (strcmp(argv[0], "bq") == 0) {
        if (argc == 3) {
            uint8_t reg = (uint8_t)strtoul(argv[1], NULL, 16);
            uint8_t val = (uint8_t)strtoul(argv[2], NULL, 16);
            printf("BQ ghi 0x%02X = 0x%02X: %s\n", reg, val, bq_reg_write(reg, val) ? "OK" : "LOI");
        } else if (argc == 2) {
            uint8_t reg = (uint8_t)strtoul(argv[1], NULL, 16), val = 0;
            bool ok = bq_reg_read(reg, &val);
            printf("BQ 0x%02X = 0x%02X%s\n", reg, val, ok ? "" : " (LOI)");
        } else {
            bq_dump();
        }
    } else if (strcmp(argv[0], "i2c") == 0) {
        i2c_scan(&hi2c1, "I2C1 (BQ25798, PB8/PB9)");
        i2c_scan(&hi2c2, "I2C2 (CN5, PB10/PB11)");
    } else if (strcmp(argv[0], "flash") == 0) {
        flash_test();
    } else if (strcmp(argv[0], "id") == 0) {
        printf("Ma node: %s\n", node_id);
    } else {
        printf("Lenh (* = cung dung duoc tu gateway qua LoRa \"@%s <lenh>\"):\n"
               "  status                  in trang thai (* = gui bao cao ngay)\n"
               "  send                    gui bao cao ve gateway ngay (*)\n"
               "  relay <1|2> <0|1>       dong/ngat relay (*)\n"
               "  time YYYY-MM-DD HH:MM:SS  xem / dat gio RTC (*)\n"
               "  mbw <slave> <addr> <val>     ghi 1 thanh ghi Modbus (*)\n"
               "  chg <0|1>               tat/bat sac pin (*)\n"
               "  mb <slave> <fc> <addr> <n>   doc thanh ghi Modbus (fc 3/4)\n"
               "  lora <noi dung>         gui goi LoRa tho\n"
               "  bq [reg [val]]          trang thai IC sac / doc / ghi thanh ghi (hex)\n"
               "  i2c                     quet 2 bus I2C\n"
               "  flash                   thu ghi/doc W25Q32\n"
               "  id                      in ma node\n", node_id);
    }
}

/*
 * Hop lenh qua SWD: debugger ghi chuoi lenh vao swd_cmd (byte 0 ghi SAU CUNG),
 * firmware chay nhu go o console roi xoa byte 0. Dung khi khong co cap USB-TTL.
 */
volatile char swd_cmd[128];

static void swd_cmd_poll(void)
{
    char buf[sizeof(swd_cmd)];
    if (swd_cmd[0] == '\0') { return; }
    for (unsigned i = 0; i < sizeof(buf); i++) { buf[i] = swd_cmd[i]; }
    buf[sizeof(buf) - 1] = '\0';
    swd_cmd[0] = '\0';
    printf("[SWD] %s\n", buf);
    cmd_exec(buf, false);
}

static void console_poll(void)
{
    int c;
    while ((c = ser_getc(SER_DBG)) >= 0) {
        if (c == '\r' || c == '\n') {
            if (con_len) {
                con_buf[con_len] = '\0';
                printf("\n");
                cmd_exec(con_buf, false);
                con_len = 0;
            }
        } else if (c == 0x08 || c == 0x7F) {
            if (con_len) { con_len--; }
        } else if (isprint(c) && con_len < sizeof(con_buf) - 1) {
            con_buf[con_len++] = (char)c;
            putchar(c);         /* echo */
        }
    }
}

/* ---------------- LoRa nhan ---------------- */

static void lora_task(void)
{
    sx1262_packet_t pkt;
    if (!sx1262_poll_rx(&pkt)) { return; }

    char msg[SX1262_MAX_PAYLOAD + 1];
    memcpy(msg, pkt.data, pkt.len);
    msg[pkt.len] = '\0';

    /* Chi xu ly lenh "@<ma> ..." gui cho minh hoac "@* ..." cho tat ca;
     * goi cua node khac ({"id":...}) thi bo qua */
    if (msg[0] != '@') { return; }
    char *sp = strchr(msg, ' ');
    if (sp == NULL) { return; }
    *sp = '\0';
    const char *to = msg + 1;
    if (strcmp(to, "*") != 0 && strcasecmp(to, node_id) != 0) { return; }

    lora_rx_count++;
    lora_cmd_count++;
    lora_last_rssi = pkt.rssi;
    lora_last_snr = pkt.snr;
    board_led3_set(true);
    t_led3 = HAL_GetTick();
    printf("[LORA] lenh tu gateway (RSSI %d SNR %d): %s\n", pkt.rssi, pkt.snr, sp + 1);
    cmd_exec(sp + 1, true);
    report_now = true;          /* moi lenh deu tra loi bang 1 goi bao cao */
}

/* ---------------- man hinh ---------------- */

static void tft_line(int row, const char *s, uint16_t color)
{
    char buf[48];
    uint8_t sc = (tft_width() >= 240) ? 2 : 1;
    int cols = tft_width() / (6 * sc);
    if (cols > (int)sizeof(buf) - 1) { cols = sizeof(buf) - 1; }
    snprintf(buf, sizeof(buf), "%-*.*s", cols, cols, s);     /* dem trang de xoa chu cu */
    tft_text(0, (uint16_t)(4 + row * 10 * sc), buf, color, TFT_BLACK, sc);
}

static void tft_update(void)
{
    char s[48];
    uint32_t up = HAL_GetTick() / 1000u;

    snprintf(s, sizeof(s), "NODE %s", node_id);
    tft_line(0, s, TFT_CYAN);
    snprintf(s, sizeof(s), "IN:%d%d%d  RELAY:%d%d", board_input_get(1), board_input_get(2),
             board_input_get(3), board_relay_get(1), board_relay_get(2));
    tft_line(1, s, TFT_WHITE);
    for (int i = 1; i <= 2; i++) {
        uint32_t r = board_flow_ml_per_min(i), t = board_flow_total_ml(i);
        snprintf(s, sizeof(s), "F%d:%lu.%02luL/m %lu.%luL", i,
                 (unsigned long)(r / 1000u), (unsigned long)((r % 1000u) / 10u),
                 (unsigned long)(t / 1000u), (unsigned long)((t % 1000u) / 100u));
        tft_line(1 + i, s, TFT_WHITE);
    }
    if (mb_st == MB_OK) {
        snprintf(s, sizeof(s), "MB:%u %u %u %u", mb_regs[0], mb_regs[1], mb_regs[2], mb_regs[3]);
    } else {
        snprintf(s, sizeof(s), "MB:%s", mb_status_str(mb_st));
    }
    tft_line(4, s, mb_st == MB_OK ? TFT_WHITE : TFT_RED);
    if (bq_ok) {
        snprintf(s, sizeof(s), "BAT:%u.%02uV %dmA %s", bq.vbat_mv / 1000u, (bq.vbat_mv % 1000u) / 10u,
                 bq.ibat_ma, bq.vbus_present ? "AC" : "");
        tft_line(5, s, bq.vbus_present ? TFT_GREEN : TFT_YELLOW);
    } else {
        tft_line(5, "BAT: BQ25798 LOI", TFT_RED);
    }
    snprintf(s, sizeof(s), "LORA:%s tx%lu rx%lu %d", lora_ok ? "OK" : "ERR",
             (unsigned long)lora_tx_count, (unsigned long)lora_rx_count, lora_last_rssi);
    tft_line(6, s, lora_ok ? TFT_WHITE : TFT_RED);
    if (clk_valid()) {
        clk_time_t t;
        clk_get(&t);
        snprintf(s, sizeof(s), "%02u/%02u/%04u %02u:%02u:%02u",
                 t.day, t.mon, t.year, t.hour, t.min, t.sec);
        tft_line(7, s, TFT_CYAN);
    } else {
        snprintf(s, sizeof(s), "UP %02lu:%02lu:%02lu", (unsigned long)(up / 3600u),
                 (unsigned long)((up / 60u) % 60u), (unsigned long)(up % 60u));
        tft_line(7, s, TFT_GRAY);
    }
}

/* IN1 -> RELAY1, IN2 -> RELAY2, co chong doi; dau vao doi thi bao ve gateway ngay */
static void input_task(void)
{
    static int8_t applied[3] = { -1, -1, -1 };     /* -1: chua ap dung lan nao */
    static bool last_raw[3];
    static uint32_t t_change[3];
    uint32_t now = HAL_GetTick();

    for (int i = 0; i < 3; i++) {
        bool raw = board_input_get(i + 1);
        if (raw != last_raw[i]) {
            last_raw[i] = raw;
            t_change[i] = now;
            continue;
        }
        if ((now - t_change[i]) < CFG_INPUT_DEBOUNCE_MS || applied[i] == (int8_t)raw) {
            continue;
        }
        bool first = applied[i] < 0;
        applied[i] = (int8_t)raw;
#if CFG_INPUT_RELAY_LINK
        if (i < 2) {
            board_relay_set(i + 1, raw);
            printf("IN%d = %d -> RELAY%d = %d\n", i + 1, raw, i + 1, raw);
        }
#endif
        if (!first) { report_now = true; }
    }
}

/* ---------------------------------------------------------------- */

void app_init(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    ser_init();
    board_led_set(true);
    board_led3_set(false);
    make_node_id();

    printf("\n\n==== AVIET NODE V1 - STM32F407VET6 @ %lu MHz, ma node %s ====\n",
           (unsigned long)(HAL_RCC_GetHCLKFreq() / 1000000u), node_id);

    flash_ok = w25q_init();
    printf("Flash W25Q: JEDEC 0x%06lX -> %s\n", (unsigned long)w25q_jedec_id(),
           flash_ok ? "OK" : "KHONG THAY (FLASH-CS chua noi PA4?)");

    if (bq_init()) {
        poll_bq();
        bq_dump();
    } else {
        printf("BQ25798: KHONG tra loi I2C1 (PB8/PB9, 0x6B)\n");
    }

    lora_ok = sx1262_init();
    if (lora_ok) {
        uint16_t e = sx1262_device_errors();
        printf("LoRa SX1262: OK %lu Hz SF%d BW%d, loi thiet bi 0x%04X%s\n",
               (unsigned long)CFG_LORA_FREQ_HZ, CFG_LORA_SF, CFG_LORA_BW, e,
               (e & 0x20u) ? " (XOSC loi -> kiem tra CFG_LORA_USE_TCXO)" : "");
        sx1262_start_rx();
    } else {
        printf("LoRa SX1262: KHONG tra loi SPI\n");
    }

    tft_init();
    tft_ok = true;

    i2c_scan(&hi2c2, "I2C2 (CN5, PB10/PB11)");
    board_flow_update();
    poll_modbus();
    tft_update();
    printf("Go \"help\" de xem lenh.\n");

    t_1s = t_mb = HAL_GetTick();
    /* Goi dau tien sau 2..2+JITTER s de cac node khoi dong cung luc khong dung nhau */
    t_last_tx = HAL_GetTick() + 2000u - CFG_REPORT_PERIOD_MS;
    schedule_periodic();
}

void app_loop(void)
{
    uint32_t now = HAL_GetTick();

    console_poll();
    swd_cmd_poll();
    input_task();
    lora_task();

    if (report_now) {
        report_now = false;
        schedule_soon();
    }
    if ((int32_t)(now - t_next_tx) >= 0) {
        send_report();
    }
    if (t_led3 && now - t_led3 >= 100u) {
        t_led3 = 0;
        board_led3_set(false);
    }
    if (now - t_1s >= 1000u) {
        t_1s = now;
        board_flow_update();
        board_led_toggle();
        poll_bq();
        tft_update();
    }
    if (now - t_mb >= CFG_MB_POLL_PERIOD_MS) {
        t_mb = now;
        poll_modbus();
    }
}
