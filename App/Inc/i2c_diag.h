/*
 * Chan doan day cam bien I2C tren CN5: bit-bang I2C thu moi cap chan trong 4 chan tin hieu
 *   chan 3 SS1 (PB0), chan 4 SS2 (PB1), chan 5 SCL (PB10), chan 6 SDA (PB11)
 * (ca 4 deu co dien tro keo len 3.3V tren board) voi dia chi 0x44 / 0x45 (SHT3x),
 * de biet cam bien co nguon khong va dang cam o chan nao. Xong thi tra I2C2 va
 * EXTI dem luu luong ve nhu cu.
 */
#ifndef I2C_DIAG_H
#define I2C_DIAG_H

void i2c_diag_find(void);

#endif
