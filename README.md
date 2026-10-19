# NODE_AVIET_V1

Firmware node (STM32F407VET6, so do `3_NODE_V1.pdf`). Giong board gateway nhung khong co 4G,
thay bang IC sac BQ25798. Node gui du lieu ve gateway `GATEWAY_AVIET_V1` qua LoRa.

- Code ung dung: `App/` (app, board, bq25798, serial, modbus, sx1262, w25q, tft, clock)
- Cau hinh: `App/Inc/config.h` (thong so LoRa phai giong het gateway)
- Console: USART1 PB6/PB7 (CN6) 115200, go `help`

## Giao thuc LoRa

Node -> gateway (moi `CFG_REPORT_PERIOD_MS` + tre ngau nhien, hoac ngay khi dau vao doi / co lenh):

```
{"id":"1A2B3C4D","n":12,"up":3600,"in":[0,1,0],"rl":[0,1],"f1":[1500,23000],"f2":[0,0],
 "vb":12450,"ib":-120,"bp":80,"vin":0,"chg":0,"rs":-87,"mb":[1,2,3]}
```

`id` ma node (tu UID chip, hoac `CFG_NODE_ID`), `n` so thu tu goi, `f1/f2` = [mL/phut, tong mL],
`vb/ib` pin mV/mA (ib duong = dang sac), `bp` % pin uoc luong theo dien ap, `vin` nguon vao mV,
`chg` trang thai sac BQ25798 (0 khong sac, 1 trickle, 2 pre, 3 CC, 4 CV, 6 top-off, 7 day),
`mp` VINDPM (mV) do MPPT dat (chi co khi MPPT bat), `bf` danh sach loi IC sac (chi co khi co loi),
`rs` RSSI goi cuoi tu gateway,
`mb` thanh ghi Modbus (loi thi `"mbe":"..."`).

Gateway -> node: chuoi chu `@<id> <lenh>` (hoac `@* <lenh>` cho moi node). Lenh: `relay 1 1`,
`send`, `time 2026-10-19 12:00:00`, `mbw <slave> <addr> <val>`, `chg 0|1`. Node tra loi bang 1 goi bao cao.
Thu tu console gateway: `lora @1A2B3C4D relay 1 1`.

## Man hinh TFT

Dong 0-3: ma node + gio, dau vao/relay/LoRa, luu luong, Modbus. Dong 4-11: IC sac BQ25798 -
trang thai sac, nguon vao (V/A), pin (V/A), % pin + thanh, VSYS + nhiet chip, gia tri dat
(so cell, VREG, ICHG, IINDPM), loi / dang bi gioi han (IINDPM, VINDPM, nhiet, VSYSMIN).

## Khong co cap USB-TTL

`tools\readlog.ps1` doc log printf trong RAM qua ST-Link; `tools\sendcmd.ps1 "<lenh>"` go lenh console
qua SWD (vd `sendcmd.ps1 "bq"`).
