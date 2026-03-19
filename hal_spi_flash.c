//#pragma GCC optimize("Ofast")

// faina pasimt svetima koda, biski pakrapstyt - viskas, tinka, subtask isspresta greiciau nei pats buciau parases, tai netgi toleruotina kad vaikisku nesamoniu yra tarkim butent sitam kode. BET, veliau gali ikast siknon svetimi uzsislepe bugai, kaip kad ivyko dabar...gaunas kad butu labiau apsimokeje paciam sita JEDEC nesamone pasirasyt.

#include "hal_spi_flash.h"
#include "stm32f3xx.h"
#include "stm32f3xx_hal_spi.h"

#define SPI_FLASH_WriteEnable	   0x06
#define SPI_FLASH_WriteDisable	   0x04
#define SPI_FLASH_ReadStatusReg	   0x05
#define SPI_FLASH_WriteStatusReg   0x01
#define SPI_FLASH_ReadData	   0x03
#define SPI_FLASH_FastReadData	   0x0B
#define SPI_FLASH_FastReadDual	   0x3B
#define SPI_FLASH_PageProgram	   0x02
#define SPI_FLASH_BlockErase	   0xD8
#define SPI_FLASH_SectorErase	   0x20
#define SPI_FLASH_ChipErase	   0xC7
#define SPI_FLASH_PowerDown	   0xB9
#define SPI_FLASH_ReleasePowerDown 0xAB
#define SPI_FLASH_DeviceID	   0xAB
#define SPI_FLASH_ManufactDeviceID 0x90
#define SPI_FLASH_JedecDeviceID	   0x9F
#define SPI_FLASH_WIP_FLAG	   0x01
#define SPI_FLASH_DUMMY_BYTE	   0xFF

#define CHOOSE_BIT_16 16
#define CHOOSE_BIT_8  8

#define SPI_FLASH_CS_ENABLE()  SPI_FLASH_CS_PORT->BRR = SPI_FLASH_CS_PIN
#define SPI_FLASH_CS_DISABLE() SPI_FLASH_CS_PORT->BSRR = SPI_FLASH_CS_PIN

#define CHECK_RET_RETURN(ret)                                                                                                                                                                          \
	do {                                                                                                                                                                                           \
		if ((ret) < 0) {                                                                                                                                                                       \
			return ret;                                                                                                                                                                    \
		}                                                                                                                                                                                      \
	} while (0)

static SPI_HandleTypeDef *g_spi_flash;
static GPIO_TypeDef* SPI_FLASH_CS_PORT;
static uint16_t SPI_FLASH_CS_PIN;
static unsigned g_capacity, g_id;

void hal_spi_flash_config(SPI_HandleTypeDef *spi, GPIO_TypeDef* gpio, uint16_t pin)
{
	g_spi_flash = spi;
	SPI_FLASH_CS_PORT = gpio;
	SPI_FLASH_CS_PIN = pin;
	g_capacity = 16777216; // default assumed size
	g_id = 0;
}

static unsigned calc_capacity(unsigned id)
{ // jedec is !@#$%^&* https://www.basicinputoutput.com/2023/11/jedec-manufacturer-ids-are-mess.html
	return (g_capacity = (id & 0x0000ff00) * 1024); // problematic with `Found Eon flash chip "EN25QH128" (16384 kB, ID=0x1C7018) on ch341a_spi.`
	// todo: either fix cap extraction from jedec ID (take flashrom's DB?), or allow overiding from webui
}

__attribute__((long_call, section(".ramtext")))
static int prv_spi_flash_send_byte(uint8_t send, uint8_t *recv)
{
	uint8_t tmp;
	uint8_t t_send = send;

	if (HAL_SPI_TransmitReceive(g_spi_flash, &t_send, &tmp, 1, HAL_MAX_DELAY) != HAL_OK) {
		return -1;
	}
	if (recv != NULL) {
		*recv = tmp;
	}

	return 0;
}

__attribute__((long_call, section(".ramtext")))
static int prv_spi_flash_send_cmd(uint8_t cmd, uint32_t addr)
{
	int ret = 0;

	ret = prv_spi_flash_send_byte(cmd, NULL);
	CHECK_RET_RETURN(ret);
	ret = prv_spi_flash_send_byte((addr & 0xFF0000) >> CHOOSE_BIT_16, NULL);
	CHECK_RET_RETURN(ret);
	ret = prv_spi_flash_send_byte((addr & 0xFF00) >> CHOOSE_BIT_8, NULL);
	CHECK_RET_RETURN(ret);
	ret = prv_spi_flash_send_byte(addr & 0xFF, NULL);
	CHECK_RET_RETURN(ret);

	return ret;
}

__attribute__((long_call, section(".ramtext")))
static void prv_spi_flash_write_enable(void)
{
	SPI_FLASH_CS_ENABLE();
	(void)prv_spi_flash_send_byte(SPI_FLASH_WriteEnable, NULL);
	SPI_FLASH_CS_DISABLE();
}

__attribute__((long_call, section(".ramtext")))
static void prv_spi_flash_wait_write_end(void)
{
	uint8_t status = 0;

	SPI_FLASH_CS_ENABLE();

	(void)prv_spi_flash_send_byte(SPI_FLASH_ReadStatusReg, NULL);

	/* Loop as long as the memory is busy with a write cycle */
	do {
		/* Send a dummy byte to generate the clock needed by the FLASH
        and put the value of the status register in status variable */
		if (prv_spi_flash_send_byte(SPI_FLASH_DUMMY_BYTE, &status) == -1) {
			break;
		}
	} while ((status & SPI_FLASH_WIP_FLAG) == SET); /* Write in progress */

	SPI_FLASH_CS_DISABLE();
}

__attribute__((long_call, section(".ramtext")))
static int prv_spi_flash_write_page(const uint8_t *buf, uint32_t addr, int32_t len)
{
	int ret = 0;
	int i;

	if (len == 0) {
		return 0;
	}

	prv_spi_flash_write_enable();
	SPI_FLASH_CS_ENABLE();

	if ((ret = prv_spi_flash_send_cmd(SPI_FLASH_PageProgram, addr)) != -1) {
		for (i = 0; i < len; ++i) {
			if (prv_spi_flash_send_byte(buf[i], NULL) == -1) {
				ret = -1;
				break;
			}
		}
	}

	SPI_FLASH_CS_DISABLE();
	prv_spi_flash_wait_write_end();

	return ret;
}

__attribute__((long_call, section(".ramtext")))
static int prv_spi_flash_erase_sector(uint32_t addr)
{
	int ret = 0;

	prv_spi_flash_write_enable();
	prv_spi_flash_wait_write_end();
	SPI_FLASH_CS_ENABLE();

	ret = prv_spi_flash_send_cmd(SPI_FLASH_SectorErase, addr);

	SPI_FLASH_CS_DISABLE();
	prv_spi_flash_wait_write_end();

	return ret;
}

__attribute__((long_call, section(".ramtext")))
unsigned hal_spi_flash_busy(void)
{
	uint8_t status = 0;

	SPI_FLASH_CS_ENABLE();

	if (prv_spi_flash_send_byte(SPI_FLASH_ReadStatusReg, NULL) || prv_spi_flash_send_byte(SPI_FLASH_DUMMY_BYTE, &status))
		return 1;

	SPI_FLASH_CS_DISABLE();

	return (status & SPI_FLASH_WIP_FLAG) == SET;
}

int hal_spi_flash_erase_whole(void)
{
	int ret = 0;

	prv_spi_flash_write_enable();
	prv_spi_flash_wait_write_end();
	SPI_FLASH_CS_ENABLE();

	ret = prv_spi_flash_send_byte(SPI_FLASH_ChipErase, NULL);

	SPI_FLASH_CS_DISABLE();

	return ret;
}

__attribute__((long_call, section(".ramtext")))
static int hal_spi_flash_erase_endptr(uint32_t addr, int32_t len, uint32_t *end)
{
	uint32_t begin;
	int i;

	if ((len < 1) || (addr >= g_capacity) || (addr + len - 1 >= g_capacity)) {
		return -1;
	}

	begin = addr / SPI_FLASH_SECTOR * SPI_FLASH_SECTOR;
	*end   = (addr + len - 1) / SPI_FLASH_SECTOR * SPI_FLASH_SECTOR;

	for (i = begin; i <= *end; i += SPI_FLASH_SECTOR) {
		if (prv_spi_flash_erase_sector(i) == -1) {
			return -1;
		}
	}

	return 0;
}

__attribute__((long_call, section(".ramtext")))
int hal_spi_flash_erase(uint32_t addr, int32_t len)
{
	uint32_t end = 0;
	return hal_spi_flash_erase_endptr(addr, len, &end);
}

__attribute__((long_call, section(".ramtext")))
int hal_spi_flash_write(const void *buf, int32_t len, uint32_t *location)
{
	const uint8_t *pbuf = (const uint8_t *)buf;
	int original_len    = len;
	int32_t loc_addr    = *location;
	int ret		    = 0;

	if (!pbuf || !location || len < 1 || *location >= g_capacity || len - 1 + *location >= g_capacity)
		return -1;

	while (len > 0) {
		uint32_t page_offset	= loc_addr % SPI_FLASH_PAGESIZE;
		uint32_t page_remaining = SPI_FLASH_PAGESIZE - page_offset;
		uint32_t chunk_len	= len < page_remaining ? len : page_remaining;

		ret = prv_spi_flash_write_page(pbuf, loc_addr, chunk_len);
		CHECK_RET_RETURN(ret);

		pbuf += chunk_len;
		loc_addr += chunk_len;
		len -= chunk_len;
	}

	*location += original_len;
	return ret;
}

__attribute__((long_call, section(".ramtext")))
int hal_spi_flash_erase_write(const void *buf, int32_t len, uint32_t *location)
{
	int ret = 0;

	ret = hal_spi_flash_erase(*location, len);
	CHECK_RET_RETURN(ret);
	ret = hal_spi_flash_write(buf, len, location);

	return ret;
}

__attribute__((long_call, section(".ramtext")))
int hal_spi_flash_erase_write_page_unaligned(const void *buf, int32_t len, uint32_t *location, uint32_t *erase_end)
{
	int ret = 0;

	for (; *location + len > *erase_end; *erase_end += SPI_FLASH_SECTOR)
		prv_spi_flash_erase_sector(*erase_end);

	CHECK_RET_RETURN(ret);
	ret = hal_spi_flash_write(buf, len, location);

	return ret;
}

__attribute__((long_call, section(".ramtext")))
int hal_spi_flash_read(void *buf, int32_t len, uint32_t location)
{
	int ret = 0;
	int i;
	uint8_t *pbuf = (uint8_t *)buf;

	if ((pbuf == NULL) || (len < 1) || (location >= g_capacity) || (len - 1 + location >= g_capacity)) {
		return -1;
	}

	SPI_FLASH_CS_ENABLE();

	if ((ret = prv_spi_flash_send_cmd(SPI_FLASH_ReadData, location)) != -1) {
		for (i = 0; i < len; ++i) {
			if (prv_spi_flash_send_byte(SPI_FLASH_DUMMY_BYTE, pbuf + i) == -1) {
				ret = -1;
				break;
			}
		}
	}

	SPI_FLASH_CS_DISABLE();

	return ret;
}
#include <stdio.h>
int hal_spi_flash_get_id(void)
{
	uint8_t tmp1 = 0;
	uint8_t tmp2 = 0;
	uint8_t tmp3 = 0;

	unsigned retry = 10;
	do {
		SPI_FLASH_CS_ENABLE();

		if (prv_spi_flash_send_byte(SPI_FLASH_JedecDeviceID, NULL) != -1) {
			(void)prv_spi_flash_send_byte(SPI_FLASH_DUMMY_BYTE, &tmp1);
			(void)prv_spi_flash_send_byte(SPI_FLASH_DUMMY_BYTE, &tmp2);
			(void)prv_spi_flash_send_byte(SPI_FLASH_DUMMY_BYTE, &tmp3);
		}

		SPI_FLASH_CS_DISABLE();

		g_id = (tmp1 << CHOOSE_BIT_16) | (tmp2 << CHOOSE_BIT_8) | tmp3;

		// kazkoke ****** ir neidomu ieskot ***** kodel is pirmo kart neranda. tai iki kol bus perrasytas visas sitas svetimas ****** i kazka normalu, va:
	} while (!g_id && retry--);

	calc_capacity(g_id);
	return g_id;
}

unsigned hal_spi_flash_get_capacity(void)
{
	if (g_capacity)
		return g_capacity;

	uint32_t ticks = HAL_GetTick();
	while (hal_spi_flash_busy() && HAL_GetTick() - ticks < 250);

	return calc_capacity(hal_spi_flash_get_id());
}

void hal_spi_flash_power_down(void)
{
	SPI_FLASH_CS_ENABLE();
	(void)prv_spi_flash_send_byte(SPI_FLASH_PowerDown, NULL);
	SPI_FLASH_CS_DISABLE();
}

void hal_spi_flash_wake_up(void)
{
	SPI_FLASH_CS_ENABLE();
	(void)prv_spi_flash_send_byte(SPI_FLASH_ReleasePowerDown, NULL);
	SPI_FLASH_CS_DISABLE();
}
