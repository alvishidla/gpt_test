#ifndef HAL_SPI_FLASH_H
#define HAL_SPI_FLASH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SPI_FLASH_PAGE_SIZE               256U
#define SPI_FLASH_3B_MAX_ADDRESS          0x00FFFFFFUL
#define SPI_FLASH_SIZE_REQUIRES_4B        (SPI_FLASH_3B_MAX_ADDRESS + 1UL)

#define SPI_FLASH_CMD_WRITE_ENABLE        0x06U
#define SPI_FLASH_CMD_READ_STATUS         0x05U
#define SPI_FLASH_CMD_PAGE_PROGRAM_3B     0x02U
#define SPI_FLASH_CMD_PAGE_PROGRAM_4B     0x12U
#define SPI_FLASH_CMD_READ_DATA_3B        0x03U
#define SPI_FLASH_CMD_READ_DATA_4B        0x13U
#define SPI_FLASH_CMD_SECTOR_ERASE_3B     0x20U
#define SPI_FLASH_CMD_SECTOR_ERASE_4B     0x21U
#define SPI_FLASH_CMD_BLOCK_ERASE_3B      0xD8U
#define SPI_FLASH_CMD_BLOCK_ERASE_4B      0xDCU
#define SPI_FLASH_CMD_ENTER_4B_MODE       0xB7U
#define SPI_FLASH_CMD_EXIT_4B_MODE        0xE9U

#ifndef SPI_FLASH_SECTOR_SIZE
#define SPI_FLASH_SECTOR_SIZE             4096U
#endif

#ifndef SPI_FLASH_BLOCK_SIZE
#define SPI_FLASH_BLOCK_SIZE              65536U
#endif

typedef enum
{
    SPI_FLASH_ADDRESS_MODE_3B = 0,
    SPI_FLASH_ADDRESS_MODE_4B = 1,
} spi_flash_address_mode_t;

typedef enum
{
    SPI_FLASH_OK = 0,
    SPI_FLASH_ERROR = -1,
    SPI_FLASH_INVALID_ARGUMENT = -2,
    SPI_FLASH_OUT_OF_RANGE = -3,
} spi_flash_status_t;

typedef struct
{
    spi_flash_status_t (*select)(void *context);
    spi_flash_status_t (*deselect)(void *context);
    spi_flash_status_t (*txrx)(void *context, const uint8_t *tx_data, uint8_t *rx_data, size_t length);
    void (*delay_ms)(void *context, uint32_t delay_ms);
    void *context;
} spi_flash_bus_t;

typedef struct
{
    spi_flash_bus_t bus;
    uint32_t size_bytes;
    spi_flash_address_mode_t address_mode;
} spi_flash_t;

spi_flash_status_t spi_flash_init(spi_flash_t *flash, const spi_flash_bus_t *bus, uint32_t size_bytes);
spi_flash_status_t spi_flash_set_address_mode(spi_flash_t *flash, spi_flash_address_mode_t address_mode);
spi_flash_address_mode_t spi_flash_get_address_mode(const spi_flash_t *flash, uint32_t address);

spi_flash_status_t spi_flash_read(spi_flash_t *flash, uint32_t address, uint8_t *data, size_t length);
spi_flash_status_t spi_flash_page_program(spi_flash_t *flash, uint32_t address, const uint8_t *data, size_t length);
spi_flash_status_t spi_flash_sector_erase(spi_flash_t *flash, uint32_t address);
spi_flash_status_t spi_flash_block_erase(spi_flash_t *flash, uint32_t address);

#ifdef __cplusplus
}
#endif

#endif
