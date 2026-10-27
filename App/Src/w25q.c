#include "w25q.h"
#include "main.h"

extern SPI_HandleTypeDef hspi1;

#define CMD_WREN        0x06
#define CMD_RDSR1       0x05
#define CMD_READ        0x03
#define CMD_PP          0x02
#define CMD_SE          0x20
#define CMD_JEDEC       0x9F
#define CMD_RELEASE_PD  0xAB

static uint32_t flash_size;

static void cs(int level)
{
    HAL_GPIO_WritePin(FLASH_CS_GPIO_Port, FLASH_CS_Pin, level ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void tx(const uint8_t *d, uint16_t n)
{
    HAL_SPI_Transmit(&hspi1, (uint8_t *)d, n, 100);
}

static void cmd_addr(uint8_t cmd, uint32_t addr)
{
    uint8_t b[4] = { cmd, (uint8_t)(addr >> 16), (uint8_t)(addr >> 8), (uint8_t)addr };
    tx(b, 4);
}

static void wait_busy(uint32_t timeout_ms)
{
    uint8_t c = CMD_RDSR1, sr = 0;
    uint32_t t0 = HAL_GetTick();
    cs(0);
    tx(&c, 1);
    do {
        HAL_SPI_Receive(&hspi1, &sr, 1, 10);
    } while ((sr & 0x01u) && (HAL_GetTick() - t0) < timeout_ms);
    cs(1);
}

static void write_enable(void)
{
    uint8_t c = CMD_WREN;
    cs(0);
    tx(&c, 1);
    cs(1);
}

uint32_t w25q_jedec_id(void)
{
    uint8_t c = CMD_JEDEC, id[3] = { 0 };
    cs(0);
    tx(&c, 1);
    HAL_SPI_Receive(&hspi1, id, 3, 10);
    cs(1);
    return ((uint32_t)id[0] << 16) | ((uint32_t)id[1] << 8) | id[2];
}

bool w25q_init(void)
{
    uint8_t c = CMD_RELEASE_PD;
    cs(0);
    tx(&c, 1);
    cs(1);
    HAL_Delay(1);

    uint32_t id = w25q_jedec_id();
    uint8_t mf = (uint8_t)(id >> 16), cap = (uint8_t)id;
    if (mf == 0x00 || mf == 0xFF || cap < 0x10 || cap > 0x20) {
        flash_size = 0;
        return false;
    }
    flash_size = 1ul << cap;   /* 0x16 -> 4 MB */
    return true;
}

uint32_t w25q_size(void)
{
    return flash_size;
}

void w25q_read(uint32_t addr, void *buf, size_t len)
{
    cs(0);
    cmd_addr(CMD_READ, addr);
    HAL_SPI_Receive(&hspi1, (uint8_t *)buf, (uint16_t)len, 1000);
    cs(1);
}

void w25q_write(uint32_t addr, const void *buf, size_t len)
{
    const uint8_t *p = (const uint8_t *)buf;
    while (len) {
        /* Khong vuot qua ranh gioi trang 256 byte */
        size_t n = W25Q_PAGE_SIZE - (addr % W25Q_PAGE_SIZE);
        if (n > len) { n = len; }
        write_enable();
        cs(0);
        cmd_addr(CMD_PP, addr);
        tx(p, (uint16_t)n);
        cs(1);
        wait_busy(10);
        addr += n;
        p += n;
        len -= n;
    }
}

void w25q_erase_sector(uint32_t addr)
{
    write_enable();
    cs(0);
    cmd_addr(CMD_SE, addr & ~(W25Q_SECTOR_SIZE - 1u));
    cs(1);
    wait_busy(500);
}
