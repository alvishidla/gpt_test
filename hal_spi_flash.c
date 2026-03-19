#include "hal_spi_flash.h"

#include <string.h>

#define SPI_FLASH_STATUS_BUSY_MASK 0x01U
#define SPI_FLASH_POLL_DELAY_MS    1U
#define SPI_FLASH_POLL_TIMEOUT_MS  5000U

static bool spi_flash_range_valid(const spi_flash_t *flash, uint32_t address, size_t length)
{
    uint64_t end_address;

    if ((flash == NULL) || (length == 0U))
    {
        return false;
    }

    end_address = (uint64_t)address + (uint64_t)length;
    return end_address <= (uint64_t)flash->size_bytes;
}

static spi_flash_status_t spi_flash_transfer(spi_flash_t *flash, const uint8_t *tx_data, uint8_t *rx_data, size_t length)
{
    spi_flash_status_t status;

    if ((flash == NULL) || (flash->bus.select == NULL) || (flash->bus.deselect == NULL) || (flash->bus.txrx == NULL))
    {
        return SPI_FLASH_INVALID_ARGUMENT;
    }

    status = flash->bus.select(flash->bus.context);
    if (status != SPI_FLASH_OK)
    {
        return status;
    }

    status = flash->bus.txrx(flash->bus.context, tx_data, rx_data, length);

    if (flash->bus.deselect(flash->bus.context) != SPI_FLASH_OK)
    {
        return SPI_FLASH_ERROR;
    }

    return status;
}

static spi_flash_status_t spi_flash_write_enable(spi_flash_t *flash)
{
    const uint8_t command = SPI_FLASH_CMD_WRITE_ENABLE;
    return spi_flash_transfer(flash, &command, NULL, sizeof(command));
}

static spi_flash_status_t spi_flash_read_status(spi_flash_t *flash, uint8_t *status_reg)
{
    uint8_t tx_buffer[2] = { SPI_FLASH_CMD_READ_STATUS, 0xFFU };
    uint8_t rx_buffer[2] = { 0U, 0U };
    spi_flash_status_t status;

    if (status_reg == NULL)
    {
        return SPI_FLASH_INVALID_ARGUMENT;
    }

    status = spi_flash_transfer(flash, tx_buffer, rx_buffer, sizeof(tx_buffer));
    if (status != SPI_FLASH_OK)
    {
        return status;
    }

    *status_reg = rx_buffer[1];
    return SPI_FLASH_OK;
}

static spi_flash_status_t spi_flash_wait_while_busy(spi_flash_t *flash)
{
    uint8_t status_reg = 0U;
    uint32_t elapsed_ms;
    spi_flash_status_t status;

    for (elapsed_ms = 0U; elapsed_ms < SPI_FLASH_POLL_TIMEOUT_MS; elapsed_ms += SPI_FLASH_POLL_DELAY_MS)
    {
        status = spi_flash_read_status(flash, &status_reg);
        if (status != SPI_FLASH_OK)
        {
            return status;
        }

        if ((status_reg & SPI_FLASH_STATUS_BUSY_MASK) == 0U)
        {
            return SPI_FLASH_OK;
        }

        if (flash->bus.delay_ms != NULL)
        {
            flash->bus.delay_ms(flash->bus.context, SPI_FLASH_POLL_DELAY_MS);
        }
    }

    return SPI_FLASH_ERROR;
}

static size_t spi_flash_build_command(const spi_flash_t *flash,
                                      uint8_t command_3b,
                                      uint8_t command_4b,
                                      uint32_t address,
                                      uint8_t *command_buffer)
{
    spi_flash_address_mode_t address_mode = spi_flash_get_address_mode(flash, address);

    command_buffer[0] = (address_mode == SPI_FLASH_ADDRESS_MODE_4B) ? command_4b : command_3b;

    if (address_mode == SPI_FLASH_ADDRESS_MODE_4B)
    {
        command_buffer[1] = (uint8_t)(address >> 24);
        command_buffer[2] = (uint8_t)(address >> 16);
        command_buffer[3] = (uint8_t)(address >> 8);
        command_buffer[4] = (uint8_t)address;
        return 5U;
    }

    command_buffer[1] = (uint8_t)(address >> 16);
    command_buffer[2] = (uint8_t)(address >> 8);
    command_buffer[3] = (uint8_t)address;
    return 4U;
}

spi_flash_status_t spi_flash_init(spi_flash_t *flash, const spi_flash_bus_t *bus, uint32_t size_bytes)
{
    if ((flash == NULL) || (bus == NULL) || (bus->select == NULL) || (bus->deselect == NULL) || (bus->txrx == NULL))
    {
        return SPI_FLASH_INVALID_ARGUMENT;
    }

    memset(flash, 0, sizeof(*flash));
    flash->bus = *bus;
    flash->size_bytes = size_bytes;
    flash->address_mode = SPI_FLASH_ADDRESS_MODE_3B;

    return spi_flash_set_address_mode(flash,
                                      (size_bytes > SPI_FLASH_SIZE_REQUIRES_4B) ? SPI_FLASH_ADDRESS_MODE_4B
                                                                               : SPI_FLASH_ADDRESS_MODE_3B);
}

spi_flash_status_t spi_flash_set_address_mode(spi_flash_t *flash, spi_flash_address_mode_t address_mode)
{
    uint8_t command;
    spi_flash_status_t status;

    if (flash == NULL)
    {
        return SPI_FLASH_INVALID_ARGUMENT;
    }

    if ((address_mode == SPI_FLASH_ADDRESS_MODE_4B) && (flash->size_bytes <= SPI_FLASH_SIZE_REQUIRES_4B))
    {
        return SPI_FLASH_OUT_OF_RANGE;
    }

    if (flash->address_mode == address_mode)
    {
        return SPI_FLASH_OK;
    }

    command = (address_mode == SPI_FLASH_ADDRESS_MODE_4B) ? SPI_FLASH_CMD_ENTER_4B_MODE : SPI_FLASH_CMD_EXIT_4B_MODE;
    status = spi_flash_transfer(flash, &command, NULL, sizeof(command));
    if (status != SPI_FLASH_OK)
    {
        return status;
    }

    flash->address_mode = address_mode;
    return SPI_FLASH_OK;
}

spi_flash_address_mode_t spi_flash_get_address_mode(const spi_flash_t *flash, uint32_t address)
{
    if ((flash != NULL) && ((flash->address_mode == SPI_FLASH_ADDRESS_MODE_4B) || (address > SPI_FLASH_3B_MAX_ADDRESS)))
    {
        return SPI_FLASH_ADDRESS_MODE_4B;
    }

    return SPI_FLASH_ADDRESS_MODE_3B;
}

spi_flash_status_t spi_flash_read(spi_flash_t *flash, uint32_t address, uint8_t *data, size_t length)
{
    uint8_t command_buffer[5];
    size_t command_length;
    spi_flash_status_t status;

    if ((data == NULL) || !spi_flash_range_valid(flash, address, length))
    {
        return SPI_FLASH_INVALID_ARGUMENT;
    }

    command_length = spi_flash_build_command(flash,
                                             SPI_FLASH_CMD_READ_DATA_3B,
                                             SPI_FLASH_CMD_READ_DATA_4B,
                                             address,
                                             command_buffer);

    status = flash->bus.select(flash->bus.context);
    if (status != SPI_FLASH_OK)
    {
        return status;
    }

    status = flash->bus.txrx(flash->bus.context, command_buffer, NULL, command_length);
    if (status == SPI_FLASH_OK)
    {
        status = flash->bus.txrx(flash->bus.context, NULL, data, length);
    }

    if (flash->bus.deselect(flash->bus.context) != SPI_FLASH_OK)
    {
        return SPI_FLASH_ERROR;
    }

    return status;
}

spi_flash_status_t spi_flash_page_program(spi_flash_t *flash, uint32_t address, const uint8_t *data, size_t length)
{
    uint8_t command_buffer[5];
    size_t command_length;
    size_t page_space;
    spi_flash_status_t status;

    if ((data == NULL) || !spi_flash_range_valid(flash, address, length))
    {
        return SPI_FLASH_INVALID_ARGUMENT;
    }

    page_space = SPI_FLASH_PAGE_SIZE - (address % SPI_FLASH_PAGE_SIZE);
    if ((length == 0U) || (length > page_space))
    {
        return SPI_FLASH_INVALID_ARGUMENT;
    }

    status = spi_flash_write_enable(flash);
    if (status != SPI_FLASH_OK)
    {
        return status;
    }

    command_length = spi_flash_build_command(flash,
                                             SPI_FLASH_CMD_PAGE_PROGRAM_3B,
                                             SPI_FLASH_CMD_PAGE_PROGRAM_4B,
                                             address,
                                             command_buffer);

    status = flash->bus.select(flash->bus.context);
    if (status != SPI_FLASH_OK)
    {
        return status;
    }

    status = flash->bus.txrx(flash->bus.context, command_buffer, NULL, command_length);
    if (status == SPI_FLASH_OK)
    {
        status = flash->bus.txrx(flash->bus.context, data, NULL, length);
    }

    if (flash->bus.deselect(flash->bus.context) != SPI_FLASH_OK)
    {
        return SPI_FLASH_ERROR;
    }

    if (status != SPI_FLASH_OK)
    {
        return status;
    }

    return spi_flash_wait_while_busy(flash);
}

spi_flash_status_t spi_flash_sector_erase(spi_flash_t *flash, uint32_t address)
{
    uint8_t command_buffer[5];
    size_t command_length;
    spi_flash_status_t status;

    if (!spi_flash_range_valid(flash, address, 1U) || ((address % SPI_FLASH_SECTOR_SIZE) != 0U))
    {
        return SPI_FLASH_INVALID_ARGUMENT;
    }

    status = spi_flash_write_enable(flash);
    if (status != SPI_FLASH_OK)
    {
        return status;
    }

    command_length = spi_flash_build_command(flash,
                                             SPI_FLASH_CMD_SECTOR_ERASE_3B,
                                             SPI_FLASH_CMD_SECTOR_ERASE_4B,
                                             address,
                                             command_buffer);

    status = spi_flash_transfer(flash, command_buffer, NULL, command_length);
    if (status != SPI_FLASH_OK)
    {
        return status;
    }

    return spi_flash_wait_while_busy(flash);
}

spi_flash_status_t spi_flash_block_erase(spi_flash_t *flash, uint32_t address)
{
    uint8_t command_buffer[5];
    size_t command_length;
    spi_flash_status_t status;

    if (!spi_flash_range_valid(flash, address, 1U) || ((address % SPI_FLASH_BLOCK_SIZE) != 0U))
    {
        return SPI_FLASH_INVALID_ARGUMENT;
    }

    status = spi_flash_write_enable(flash);
    if (status != SPI_FLASH_OK)
    {
        return status;
    }

    command_length = spi_flash_build_command(flash,
                                             SPI_FLASH_CMD_BLOCK_ERASE_3B,
                                             SPI_FLASH_CMD_BLOCK_ERASE_4B,
                                             address,
                                             command_buffer);

    status = spi_flash_transfer(flash, command_buffer, NULL, command_length);
    if (status != SPI_FLASH_OK)
    {
        return status;
    }

    return spi_flash_wait_while_busy(flash);
}
