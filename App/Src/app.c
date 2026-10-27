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
#include "es_rain.h"
#include "i2c_diag.h"
#include "modbus.h"
#include "serial.h"
#include "sht3x.h"
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

static bq_info_t bq;
static bool bq_ok;

/* Cam bien mua ES-RAIN-02 (RS485) */
static mb_status_t rain_st = MB_ERR_TIMEOUT;
static bool raining;

/* SHT30 (I2C2) */
static bool sht_ok;
static int16_t sht_t_x10;
static uint16_t sht_rh_x10;

static uint32_t lora_tx_count, lora_tx_fail, lora_rx_count, lora_cmd_count;
static uint16_t lora_seq;
static int16_t lora_last_rssi;
static int8_t lora_last_snr;

static uint32_t t_1s, t_mb, t_led3, t_rain, t_sht;
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

__attribute__((unused))     /* chi dung khi CFG_MB_POLL_ENABLE = 1 */
static void poll_modbus(void)
{
    mb_st = mb_read_regs(CFG_MB_SLAVE, CFG_MB_FUNC, CFG_MB_START_REG, CFG_MB_REG_COUNT, mb_regs);
}

/* Cam bien mua: doi trang thai (hoac mat/co lai ket noi) thi log va bao ve gateway ngay */
static void poll_rain(void)
{
#if CFG_RAIN_ENABLE
    static bool first = true;
    bool r = raining;
    mb_status_t st = rain_read(&r);

    if (st != rain_st && (first || st == MB_OK || rain_st == MB_OK)) {
        if (st == MB_OK) {
            printf("[MUA] Cam bien tra loi (dia chi %d, %lu baud)\n", CFG_RAIN_ADDR,
                   (unsigned long)mb_baud());
        } else {
            printf("[MUA] Khong doc duoc cam bien: %s\n", mb_status_str(st));
        }
        if (!first) { report_now = true; }
    }
    if (st == MB_OK && (r != raining || first || rain_st != MB_OK)) {
        if (r != raining || first) {
            printf("[MUA] %s\n", r ? "CO MUA / TUYET" : "Khong mua");
        }
        if (r != raining && !first) { report_now = true; }
        raining = r;
    }
    rain_st = st;
    first = false;
#endif
}

static void print_rain_cfg(void)
{
    rain_cfg_t c;
    mb_status_t st = rain_read_cfg(&c);
    if (st != MB_OK) {
        printf("Cam bien mua: doc cau hinh loi (%s)\n", mb_status_str(st));
        return;
    }
    printf("Cam bien mua: %s | suoi bat <%d.%d C, tat >%d.%d C, tre %u.%u C | xac nhan %u s | do nhay %u\n",
           rain_st == MB_OK ? (raining ? "CO MUA" : "khong mua") : mb_status_str(rain_st),
           c.heat_min_x10 / 10, (c.heat_min_x10 < 0 ? -c.heat_min_x10 : c.heat_min_x10) % 10,
           c.heat_max_x10 / 10, (c.heat_max_x10 < 0 ? -c.heat_max_x10 : c.heat_max_x10) % 10,
           c.heat_hys_x10 / 10u, c.heat_hys_x10 % 10u, c.delay_s, c.sensitivity);
}

/* SHT30: mat ket noi thi 10 s thu tim lai (cam nong vao CN5 cung duoc) */
static void poll_sht(void)
{
#if CFG_SHT_ENABLE
    static uint32_t t_retry;
    if (!sht3x_present()) {
        if (t_retry != 0 && (HAL_GetTick() - t_retry) < 10000u) { return; }
        t_retry = HAL_GetTick();
        if (!sht3x_init()) { return; }
        printf("[SHT] Thay SHT3x o 0x%02X\n", sht3x_addr());
    }
    int16_t t;
    uint16_t h;
    bool ok = sht3x_read(&t, &h);
    if (ok) {
        sht_t_x10 = t;
        sht_rh_x10 = h;
    } else if (sht_ok) {
        printf("[SHT] Doc loi, thu tim lai cam bien\n");
        sht3x_init();
        t_retry = HAL_GetTick();
    }
    sht_ok = ok;
#endif
}

/* Doc IC sac moi giay; in ra console khi trang thai sac / nguon vao / loi thay doi */
static void poll_bq(void)
{
    static bool first = true;
    static uint8_t last_chg;
    static bool last_vbus, last_mppt;
    static uint16_t last_vindpm;
    static char last_fault[96];
    char fault[96];

    bool ok = bq_read(&bq);
    if (ok != bq_ok && !first) {
        printf("[BQ] %s\n", ok ? "IC sac tra loi lai" : "IC sac MAT KET NOI I2C");
    }
    bq_ok = ok;
    if (!ok) { return; }

    {   /* Nguon vao yeu -> chip HIZ: chi log khi doi trang thai, thu lai thi im lang */
        static bool last_weak, last_pg;
        bool weak = bq.vbus_present && !bq.power_good && bq.en_hiz;
        if (weak && !last_weak) {
            printf("[BQ] Nguon vao YEU: VBUS %u mV khong giu duoc khi tai 10 mA -> chip HIZ, khong sac"
                   " (thu lai moi %lu s)\n", bq.vbus_mv, (unsigned long)(CFG_BQ_HIZ_RETRY_MS / 1000u));
        }
        if (bq.power_good && !last_pg && !first) {
            printf("[BQ] Nguon vao DAT (PG), VBUS %u mV\n", bq.vbus_mv);
        }
        last_weak = weak;
        last_pg = bq.power_good;
        bq_hiz_retry_task(&bq);
    }
    if (bq_mppt_task(&bq)) {
        bq_read(&bq);
        printf("[BQ] Bat MPPT (VBUS %u mV) -> %s\n", bq.vbus_mv, bq.mppt_on ? "OK" : "chip tu choi");
    }
    if (!first && bq.mppt_on != last_mppt && !bq.mppt_on) {
        printf("[BQ] MPPT tat (%s)\n", bq.vbus_present ? "VBAT < VSYSMIN?" : "mat nguon vao");
    }
    if (bq.mppt_on && bq.vindpm_mv != last_vindpm) {
        /* Moi lan do VOC chip dat lai VINDPM = VOC_PCT * VOC */
        printf("[BQ] MPPT: VINDPM %u -> %u mV\n", last_vindpm, bq.vindpm_mv);
    }
    last_mppt = bq.mppt_on;
    last_vindpm = bq.vindpm_mv;

    bq_fault_str(&bq, fault, sizeof(fault));
    if (!first) {
        if (bq.vbus_present != last_vbus) {
            printf("[BQ] Nguon vao: %s (%u mV, %s)\n", bq.vbus_present ? "CAM VAO" : "RUT RA",
                   bq.vbus_mv, bq_vbus_str(bq.vbus_stat));
            report_now = true;
        }
        if (bq.chg_stat != last_chg) {
            printf("[BQ] Sac: %s -> %s (VBAT %u mV, IBAT %d mA)\n", bq_chg_str(last_chg),
                   bq_chg_str(bq.chg_stat), bq.vbat_mv, bq.ibat_ma);
        }
        if (strcmp(fault, last_fault) != 0) {
            printf("[BQ] Loi: %s\n", fault[0] ? fault : "het loi");
            if (fault[0]) { report_now = true; }
        }
    }
    first = false;
    last_vbus = bq.vbus_present;
    last_chg = bq.chg_stat;
    snprintf(last_fault, sizeof(last_fault), "%s", fault);
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
#if CFG_RAIN_ENABLE
    printf("Mua    : %s (ES-RAIN-02 dia chi %d, %lu baud)\n",
           rain_st == MB_OK ? (raining ? "CO MUA" : "khong mua") : mb_status_str(rain_st),
           CFG_RAIN_ADDR, (unsigned long)mb_baud());
#endif
#if CFG_SHT_ENABLE
    if (sht_ok) {
        printf("SHT30  : %d.%d C, %u.%u %%RH (0x%02X)\n", sht_t_x10 / 10,
               (sht_t_x10 < 0 ? -sht_t_x10 : sht_t_x10) % 10, sht_rh_x10 / 10u, sht_rh_x10 % 10u,
               sht3x_addr());
    } else {
        printf("SHT30  : %s\n", sht3x_present() ? "doc loi" : "khong thay tren I2C2 (CN5)");
    }
#endif
#if CFG_MB_POLL_ENABLE
    printf("Modbus : slave %d FC%d -> %s", CFG_MB_SLAVE, CFG_MB_FUNC, mb_status_str(mb_st));
    if (mb_st == MB_OK) {
        for (int i = 0; i < CFG_MB_REG_COUNT; i++) { printf(" %u", mb_regs[i]); }
    }
    printf("\n");
#endif
    if (bq_ok) {
        printf("Pin    : VBAT %u mV (~%u%%), IBAT %d mA, VIN %u mV, %s\n",
               bq.vbat_mv, bq_battery_percent(&bq), bq.ibat_ma, bq.vbus_mv, bq_chg_str(bq.chg_stat));
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
 *  "rain":0,"t":253,"h":651,"vb":12450,"ib":-120,"bp":80,"vin":0,"chg":0,"rs":-87}
 *   f1/f2 = [mL/phut, tong mL]; rain = 1 co mua (loi thi "rne":"<loi>");
 *   t / h = nhiet do 0.1 C / do am 0.1 %RH tu SHT30 (khong co neu loi);
 *   vb/ib = pin mV/mA; bp = % pin uoc luong; vin = nguon vao mV;
 *   chg = trang thai sac; "mp":<VINDPM mV> chi co khi MPPT dang bat;
 *   "bf":"<loi>" chi co khi IC sac bao loi;
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
#if CFG_RAIN_ENABLE
    if (rain_st == MB_OK) {
        n += snprintf(buf + n, len - (size_t)n, ",\"rain\":%d", raining);
    } else {
        n += snprintf(buf + n, len - (size_t)n, ",\"rne\":\"%s\"", mb_status_str(rain_st));
    }
#endif
    if (sht_ok) {
        n += snprintf(buf + n, len - (size_t)n, ",\"t\":%d,\"h\":%u", sht_t_x10, sht_rh_x10);
    }
    if (bq_ok) {
        char f[40];
        n += snprintf(buf + n, len - (size_t)n, ",\"vb\":%u,\"ib\":%d,\"bp\":%u,\"vin\":%u,\"chg\":%u",
                      bq.vbat_mv, bq.ibat_ma, bq_battery_percent(&bq), bq.vbus_mv, bq.chg_stat);
        if (bq.mppt_on) {
            n += snprintf(buf + n, len - (size_t)n, ",\"mp\":%u", bq.vindpm_mv);
        }
        if (bq_fault_str(&bq, f, sizeof(f))) {
            n += snprintf(buf + n, len - (size_t)n, ",\"bf\":\"%s\"", f);
        }
    }
    if (lora_rx_count) {
        n += snprintf(buf + n, len - (size_t)n, ",\"rs\":%d", lora_last_rssi);
    }
#if CFG_MB_POLL_ENABLE
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
#endif
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
#if CFG_MB_POLL_ENABLE
        poll_modbus();
#endif
        report_now = true;
    } else if (strcmp(argv[0], "chg") == 0 && argc == 2) {
        bool on = atoi(argv[1]) != 0;
        printf("Sac pin: %s -> %s\n", on ? "bat" : "tat", bq_charge_enable(on) ? "OK" : "LOI");
        report_now = true;
    } else if (strcmp(argv[0], "rain") == 0) {
        static const struct { const char *name; uint16_t reg; } keys[] = {
            { "hmax", RAIN_REG_HEAT_MAX }, { "hmin", RAIN_REG_HEAT_MIN }, { "hys", RAIN_REG_HEAT_HYS },
            { "delay", RAIN_REG_DELAY },   { "sens", RAIN_REG_SENS },
        };
        if (argc == 3) {
            unsigned i;
            for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
                if (strcmp(argv[1], keys[i].name) == 0) { break; }
            }
            if (i == sizeof(keys) / sizeof(keys[0])) {
                printf("rain <hmax|hmin|hys|delay|sens> <gia tri>\n");
                return;
            }
            /* hmax/hmin/hys nhap theo do C (vd 35 hoac 35.5), thanh ghi luu 0.1 C */
            int16_t v = (keys[i].reg <= RAIN_REG_HEAT_HYS)
                        ? (int16_t)(strtof(argv[2], NULL) * 10.0f + (argv[2][0] == '-' ? -0.5f : 0.5f))
                        : (int16_t)atoi(argv[2]);
            mb_status_t st = rain_write(keys[i].reg, v);
            printf("Cam bien mua ghi %s = %s: %s\n", argv[1], argv[2], mb_status_str(st));
        }
        print_rain_cfg();
    } else if (strcmp(argv[0], "sht") == 0) {
        if (argc == 3 && strcmp(argv[1], "heat") == 0) {
            bool on = atoi(argv[2]) != 0;
            printf("SHT bo suoi: %s -> %s\n", on ? "bat" : "tat", sht3x_heater(on) ? "OK" : "LOI");
            return;
        }
        if (!sht3x_present() && !sht3x_init()) {
            printf("SHT3x: khong thay o 0x44/0x45 tren I2C2 (CN5)\n");
            return;
        }
        int16_t t;
        uint16_t h, stat = 0;
        bool ok = sht3x_read(&t, &h);
        sht3x_read_status(&stat);
        if (ok) {
            printf("SHT3x 0x%02X: %d.%d C, %u.%u %%RH, status 0x%04X%s\n", sht3x_addr(), t / 10,
                   (t < 0 ? -t : t) % 10, h / 10u, h % 10u, stat, (stat & 0x2000u) ? " (dang suoi)" : "");
        } else {
            printf("SHT3x 0x%02X: doc loi (I2C/CRC)\n", sht3x_addr());
        }
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
    } else if (strcmp(argv[0], "i2cfind") == 0) {
        i2c_diag_find();
        if (!sht3x_present() && sht3x_init()) { printf("SHT3x: thay o 0x%02X\n", sht3x_addr()); }
    } else if (strcmp(argv[0], "flash") == 0) {
        flash_test();
    } else if (strcmp(argv[0], "id") == 0) {
        printf("Ma node: %s\n", node_id);
    } else if (strcmp(argv[0], "baud") == 0) {
        if (argc == 2) {
            mb_init((uint32_t)atol(argv[1]));
            ser_init();                     /* bat lai nhan ngat cho UART4 */
        }
        printf("RS485: %lu baud (mac dinh CFG_RS485_BAUD = %lu)\n", (unsigned long)mb_baud(),
               (unsigned long)CFG_RS485_BAUD);
    } else {
        printf("Lenh (* = cung dung duoc tu gateway qua LoRa \"@%s <lenh>\"):\n"
               "  status                  in trang thai (* = gui bao cao ngay)\n"
               "  send                    gui bao cao ve gateway ngay (*)\n"
               "  relay <1|2> <0|1>       dong/ngat relay (*)\n"
               "  time YYYY-MM-DD HH:MM:SS  xem / dat gio RTC (*)\n"
               "  mbw <slave> <addr> <val>     ghi 1 thanh ghi Modbus (*)\n"
               "  chg <0|1>               tat/bat sac pin (*)\n"
               "  rain [hmax|hmin|hys|delay|sens <gt>]  cam bien mua: xem / dat cau hinh (*)\n"
               "                          hmax/hmin/hys: do C (vd 35.5), delay: giay, sens: 500..3500\n"
               "  sht [heat 0|1]          doc SHT30 / bat-tat bo suoi trong chip (*)\n"
               "  mb <slave> <fc> <addr> <n>   doc thanh ghi Modbus (fc 3/4)\n"
               "  baud [toc do]           xem / doi toc do RS485 (tam thoi)\n"
               "  lora <noi dung>         gui goi LoRa tho\n"
               "  bq [reg [val]]          trang thai IC sac / doc / ghi thanh ghi (hex)\n"
               "  i2c                     quet 2 bus I2C\n"
               "  i2cfind                 do day SHT30 tren CN5 (thu moi cap chan 3-6)\n"
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

static uint8_t tft_scale(void)
{
    return (tft_width() >= 240) ? 2 : 1;
}

/* Thanh % ngang tren dong row, bat dau tu cot col (khong ve lai chu de khoi nhay) */
static void tft_bar(int row, int col, uint8_t pct, uint16_t color)
{
    uint8_t sc = tft_scale();
    uint16_t x = (uint16_t)(col * 6 * sc);
    uint16_t y = (uint16_t)(4 + row * 10 * sc);
    uint16_t h = (uint16_t)(7 * sc);
    if (tft_width() <= x + 8u) { return; }
    uint16_t w = (uint16_t)(tft_width() - x - 4u);
    uint16_t in_w = (uint16_t)(w - 4u);
    uint16_t fill = (uint16_t)((uint32_t)in_w * (pct > 100 ? 100 : pct) / 100u);

    tft_fill_rect(x, y, w, 1, TFT_WHITE);                       /* khung */
    tft_fill_rect(x, (uint16_t)(y + h - 1u), w, 1, TFT_WHITE);
    tft_fill_rect(x, y, 1, h, TFT_WHITE);
    tft_fill_rect((uint16_t)(x + w - 1u), y, 1, h, TFT_WHITE);
    tft_fill_rect((uint16_t)(x + 2u), (uint16_t)(y + 2u), fill, (uint16_t)(h - 4u), color);
    tft_fill_rect((uint16_t)(x + 2u + fill), (uint16_t)(y + 2u), (uint16_t)(in_w - fill),
                  (uint16_t)(h - 4u), TFT_BLACK);
}

/* "12.34V" */
static void fmt_v(char *b, size_t n, uint16_t mv)
{
    snprintf(b, n, "%u.%02uV", mv / 1000u, (mv % 1000u) / 10u);
}

/* "-49mA" hoac "1.25A" */
static void fmt_i(char *b, size_t n, int16_t ma)
{
    int a = ma < 0 ? -ma : ma;
    if (a < 1000) {
        snprintf(b, n, "%dmA", ma);
    } else {
        snprintf(b, n, "%s%d.%02dA", ma < 0 ? "-" : "", a / 1000, (a % 1000) / 10);
    }
}

/* Dong 0..3: node; dong 4..11: IC sac BQ25798 */
static void tft_update(void)
{
    char s[96], a[24], b[24], c[24];
    uint32_t up = HAL_GetTick() / 1000u;

    if (clk_valid()) {
        clk_time_t t;
        clk_get(&t);
        snprintf(s, sizeof(s), "NODE %s %02u:%02u:%02u", node_id, t.hour, t.min, t.sec);
    } else {
        snprintf(s, sizeof(s), "NODE %s UP%02lu:%02lu", node_id, (unsigned long)(up / 3600u),
                 (unsigned long)((up / 60u) % 60u));
    }
    tft_line(0, s, TFT_CYAN);
    if (lora_ok) {
        snprintf(s, sizeof(s), "IN:%d%d%d RL:%d%d TX:%lu %d", board_input_get(1), board_input_get(2),
                 board_input_get(3), board_relay_get(1), board_relay_get(2),
                 (unsigned long)lora_tx_count, lora_last_rssi);
    } else {
        snprintf(s, sizeof(s), "IN:%d%d%d RL:%d%d LORA LOI", board_input_get(1), board_input_get(2),
                 board_input_get(3), board_relay_get(1), board_relay_get(2));
    }
    tft_line(1, s, lora_ok ? TFT_WHITE : TFT_RED);
    {
        uint32_t r1 = board_flow_ml_per_min(1), r2 = board_flow_ml_per_min(2);
        snprintf(s, sizeof(s), "F1:%lu.%02lu F2:%lu.%02lu L/m",
                 (unsigned long)(r1 / 1000u), (unsigned long)((r1 % 1000u) / 10u),
                 (unsigned long)(r2 / 1000u), (unsigned long)((r2 % 1000u) / 10u));
        tft_line(2, s, TFT_WHITE);
    }
    /* 3: mua + nhiet do / do am */
    {
        bool rain_ok = rain_st == MB_OK;
        snprintf(a, sizeof(a), "MUA:%s", !rain_ok ? "LOI" : raining ? "CO" : "KO");
        if (sht_ok) {
            snprintf(b, sizeof(b), "%d.%dC %u.%u%%", sht_t_x10 / 10, (sht_t_x10 < 0 ? -sht_t_x10 : sht_t_x10) % 10,
                     sht_rh_x10 / 10u, sht_rh_x10 % 10u);
        } else {
            snprintf(b, sizeof(b), "T/H:LOI");
        }
        snprintf(s, sizeof(s), "%s %s", a, b);
        tft_line(3, s, (!rain_ok && !sht_ok) ? TFT_RED : (!rain_ok || !sht_ok) ? TFT_YELLOW
                       : raining ? TFT_CYAN : TFT_WHITE);
    }

    tft_line(4, "--- SAC PIN BQ25798 ---", TFT_YELLOW);
    if (!bq_ok) {
        tft_line(5, "IC SAC KHONG TRA LOI", TFT_RED);
        for (int r = 6; r <= 11; r++) { tft_line(r, "", TFT_BLACK); }
        return;
    }

    /* 5: trang thai sac */
    bool charging = bq.chg_stat >= 1 && bq.chg_stat <= 6;
    snprintf(s, sizeof(s), "TT: %s%s", bq_chg_str(bq.chg_stat), bq.en_chg ? "" : " (TAT)");
    tft_line(5, s, charging ? TFT_GREEN : bq.chg_stat == 7 ? TFT_CYAN : TFT_WHITE);

    /* 6: nguon vao */
    if (bq.vbus_present) {
        fmt_v(a, sizeof(a), bq.vbus_mv);
        fmt_i(b, sizeof(b), bq.ibus_ma);
        snprintf(s, sizeof(s), "VAO: %s %s%s%s", a, b, bq.mppt_on ? " MPPT" : "",
                 bq.power_good ? "" : " !PG");
        tft_line(6, s, bq.power_good ? TFT_GREEN : TFT_YELLOW);
    } else {
        tft_line(6, "VAO: KHONG CO NGUON", TFT_GRAY);
    }

    /* 7: pin */
    if (bq.vbat_present) {
        fmt_v(a, sizeof(a), bq.vbat_mv);
        fmt_i(b, sizeof(b), bq.ibat_ma);
        snprintf(s, sizeof(s), "PIN: %s %s", a, b);
        tft_line(7, s, bq.ibat_ma > 0 ? TFT_GREEN : TFT_WHITE);
    } else {
        tft_line(7, "PIN: KHONG CO PIN", TFT_RED);
    }

    /* 8: % pin + thanh */
    {
        uint8_t pct = bq.vbat_present ? bq_battery_percent(&bq) : 0;
        uint16_t col = pct > 50 ? TFT_GREEN : pct > 20 ? TFT_YELLOW : TFT_RED;
        snprintf(s, sizeof(s), "%3u%% ", pct);
        tft_text(0, (uint16_t)(4 + 8 * 10 * tft_scale()), s, col, TFT_BLACK, tft_scale());
        tft_bar(8, 5, pct, col);
    }

    /* 9: he thong + nhiet chip */
    fmt_v(a, sizeof(a), bq.vsys_mv);
    snprintf(s, sizeof(s), "SYS: %s CHIP %d.%dC", a, bq.tdie_x10 / 10,
             (bq.tdie_x10 < 0 ? -bq.tdie_x10 : bq.tdie_x10) % 10);
    tft_line(9, s, bq.treg ? TFT_YELLOW : TFT_WHITE);

    /* 10: gia tri dat */
    fmt_v(a, sizeof(a), bq.vreg_mv);
    fmt_i(b, sizeof(b), (int16_t)bq.ichg_ma);
    if (bq.mppt_on) {
        /* MPPT: hien VINDPM chip vua dat theo VOC tam pin */
        snprintf(c, sizeof(c), "M%u.%uV", bq.vindpm_mv / 1000u, (bq.vindpm_mv % 1000u) / 100u);
    } else {
        char t[12];
        fmt_i(t, sizeof(t), (int16_t)bq.iindpm_ma);
        snprintf(c, sizeof(c), "IN%s", t);
    }
    snprintf(s, sizeof(s), "DAT:%uS %s %s %s", bq.cells, a, b, c);
    tft_line(10, s, TFT_GRAY);

    /* 11: loi, neu khong co thi bao dang bi gioi han gi */
    char f[40];
    if (bq_fault_str(&bq, f, sizeof(f))) {
        snprintf(s, sizeof(s), "LOI: %s", f);
        tft_line(11, s, TFT_RED);
    } else if (bq.iindpm_reg || bq.vindpm_reg || bq.treg || bq.vsys_min_reg) {
        snprintf(s, sizeof(s), "GH:%s%s%s%s", bq.iindpm_reg ? " IINDPM" : "", bq.vindpm_reg ? " VINDPM" : "",
                 bq.treg ? " NHIET" : "", bq.vsys_min_reg ? " VSYSMIN" : "");
        tft_line(11, s, TFT_YELLOW);
    } else {
        tft_line(11, "LOI: KHONG", TFT_GREEN);
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
    mb_init(CFG_RS485_BAUD);            /* phai truoc ser_init: khoi tao lai UART4 */
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
#if CFG_SHT_ENABLE
    if (sht3x_init()) {
        poll_sht();
        printf("SHT3x: 0x%02X, %d.%d C, %u.%u %%RH\n", sht3x_addr(), sht_t_x10 / 10,
               (sht_t_x10 < 0 ? -sht_t_x10 : sht_t_x10) % 10, sht_rh_x10 / 10u, sht_rh_x10 % 10u);
    } else {
        printf("SHT3x: KHONG THAY tren I2C2 (CN5), se thu lai moi 10 s\n");
    }
#endif
#if CFG_RAIN_ENABLE
    poll_rain();
    if (rain_st == MB_OK) { print_rain_cfg(); }
#endif
    board_flow_update();
#if CFG_MB_POLL_ENABLE
    poll_modbus();
#endif
    tft_update();
    printf("Go \"help\" de xem lenh.\n");

    t_1s = t_mb = t_rain = HAL_GetTick();
    t_sht = t_rain - CFG_SHT_POLL_MS / 2u;     /* lech pha voi cam bien mua */
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
#if CFG_RAIN_ENABLE
    if (now - t_rain >= CFG_RAIN_POLL_MS) {
        t_rain = now;
        poll_rain();
    }
#endif
#if CFG_SHT_ENABLE
    if (now - t_sht >= CFG_SHT_POLL_MS) {
        t_sht = now;
        poll_sht();
    }
#endif
#if CFG_MB_POLL_ENABLE
    if (now - t_mb >= CFG_MB_POLL_PERIOD_MS) {
        t_mb = now;
        poll_modbus();
    }
#endif
}
