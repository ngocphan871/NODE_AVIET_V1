/*
 * Cau hinh cua node - sua o day, khong can dong vao driver.
 */
#ifndef CONFIG_H
#define CONFIG_H

/* ===================== Node ===================== */
/* Ma node (8 chu so hex). 0 = tu sinh tu UID 96 bit cua chip, moi board 1 ma khac nhau. */
#define CFG_NODE_ID             0x00000000u
#define CFG_REPORT_PERIOD_MS    60000u      /* chu ky gui du lieu ve gateway */
/* Them tre ngau nhien 0..JITTER de nhieu node khong phat cung luc */
#define CFG_REPORT_JITTER_MS    5000u
/* Khoang cach toi thieu giua 2 goi (khi dau vao doi lien tuc) de khong chiem kenh */
#define CFG_REPORT_MIN_GAP_MS   3000u

/* ===================== RS485 Modbus RTU ===================== */
/* Moi thiet bi tren bus phai cung toc do. ES-RAIN-02 mac dinh 4800 (doi duoc 2400/4800/9600). */
#define CFG_RS485_BAUD          4800u

/* Cam bien mua ES-RAIN-02 */
#define CFG_RAIN_ENABLE         1
#define CFG_RAIN_ADDR           1           /* dia chi Modbus, mac dinh 0x01 */
#define CFG_RAIN_POLL_MS        2000u

/* Doc thanh ghi Modbus chung (thiet bi khac tren bus, VD dong ho dien). 0 = tat.
 * Neu bat thi CFG_MB_SLAVE phai khac CFG_RAIN_ADDR. */
#define CFG_MB_POLL_ENABLE      0
#define CFG_MB_SLAVE            2           /* dia chi slave can doc */
#define CFG_MB_FUNC             3           /* 3 = holding, 4 = input register */
#define CFG_MB_START_REG        0
#define CFG_MB_REG_COUNT        10          /* toi da MB_MAX_REGS (32); goi LoRa chi chua 16 */
#define CFG_MB_TIMEOUT_MS       200u        /* cho tra loi; 4800 baud: khung 7 byte ~15 ms */
#define CFG_MB_POLL_PERIOD_MS   5000u

/* ===================== Nhiet do / do am SHT30 (CN5, I2C2) ===================== */
#define CFG_SHT_ENABLE          1
#define CFG_SHT_POLL_MS         2000u

/* ===================== LoRa SX1262 (module S62F) ===================== */
/* PHAI GIONG HET gateway (GATEWAY_AVIET_V1/App/Inc/config.h) thi moi nghe duoc nhau */
#define CFG_LORA_FREQ_HZ        433000000u  /* doi theo bang tan module: 433/470/868/915/923 MHz */
#define CFG_LORA_SF             9           /* 5..12 */
#define CFG_LORA_BW             125         /* kHz: 125, 250, 500 */
#define CFG_LORA_CR             5           /* 4/5 .. 4/8 -> 5..8 */
#define CFG_LORA_TX_DBM         22          /* -9..22 */
#define CFG_LORA_PREAMBLE       8
#define CFG_LORA_SYNC_PUBLIC    0           /* 0 = private 0x1424, 1 = public/LoRaWAN 0x3444 */
/* Gateway da kiem: module S62F dung TCXO cap boi DIO3 (TCXO=0 -> loi XOSC_START 0x0020) */
#define CFG_LORA_USE_TCXO       1
/* Chuyen mach anten (RF_VCC1 = PC9, RF_VCC2 = PA10), giong gateway */
#define CFG_LORA_TX_VC1         1
#define CFG_LORA_TX_VC2         0
#define CFG_LORA_RX_VC1         0
#define CFG_LORA_RX_VC2         1

/* ===================== IC sac BQ25798 ===================== */
#define CFG_BQ_CHARGE_ENABLE    1           /* 1 = cho phep sac pin khi co nguon vao */
/* Board khong co NTC tren chan TS (chi co cau chia R6/R9) -> phai bo qua TS, neu khong
 * chip coi la pin qua lanh va khong sac. Dat 0 neu sau nay gan NTC 10k. */
#define CFG_BQ_TS_IGNORE        1
/* 0 = giu gia tri mac dinh cua chip (theo so cell do PROG chon). Dat theo loai pin:
 * vd 1 cell Li-ion 4200 mV, 2 cell 8400 mV; LiFePO4 1 cell 3600 mV. */
#define CFG_BQ_VREG_MV          0u          /* dien ap sac day, 10 mV/buoc */
#define CFG_BQ_ICHG_MA          0u          /* dong sac, 10 mA/buoc (50..5000) */
#define CFG_BQ_IINDPM_MA        0u          /* gioi han dong lay tu nguon vao, 10 mA/buoc */
/* MPPT cho tam pin mat troi cam vao jack DC (datasheet 7.3.6.3): chip dinh ky ngat bo bien doi,
 * do ap ho mach VOC cua tam pin roi dat VINDPM = VOC_PCT * VOC. Voi adapter thi vo hai
 * (chi tam dung sac VOC_DLY moi VOC_RATE). Chip khong cho bat khi VBAT < VSYSMIN va tu tat
 * khi rut nguon, nen firmware tu bat lai moi khi co nguon vao.
 * MPPT chi tac dong khi tam pin khong cap du ICHG: muon lay het cong suat tam pin thi dat
 * CFG_BQ_ICHG_MA cao (trong gioi han cua pin). */
#define CFG_BQ_MPPT             1
#define CFG_BQ_VOC_PCT          5           /* VINDPM/VOC: 0=56.25% 1=62.5% 2=68.75% 3=75% 4=81.25% 5=87.5% 6=93.75% 7=100% */
#define CFG_BQ_VOC_DLY          1           /* cho truoc khi do VOC: 0=50ms 1=300ms 2=2s 3=5s */
#define CFG_BQ_VOC_RATE         1           /* chu ky do VOC: 0=30s 1=2phut 2=10phut 3=30phut */

/* Loai pin de uoc luong % tren man hinh: 0 = Li-ion/Li-po (3.3..4.2 V/cell), 1 = LiFePO4 */
#define CFG_BAT_CHEMISTRY       0

/* ===================== Dau vao -> relay ===================== */
/* 1: IN1 dieu khien RELAY1, IN2 dieu khien RELAY2 (relay theo trang thai dau vao,
 * chi tac dong khi dau vao thay doi nen lenh tu gateway van dung duoc giua 2 lan doi). */
#define CFG_INPUT_RELAY_LINK    1
#define CFG_INPUT_DEBOUNCE_MS   50u

/* ===================== Cam bien luu luong YF-S201C ===================== */
/* So xung tren 1 lit. YF-S201: F(Hz) = 7.5 * Q(L/phut) -> 450 xung/lit.
 * Can hieu chinh lai voi dong cam bien thuc te. */
#define CFG_FLOW_PULSES_PER_L   450u

/* ===================== Man hinh TFT (CN8) ===================== */
/* Chon 1 trong 3: TFT_ST7789 (240x320), TFT_ILI9341 (240x320), TFT_ST7735 (128x160) */
#define CFG_TFT_CONTROLLER      TFT_ST7789
#define CFG_TFT_ROTATION        1           /* 0..3 */

#endif
