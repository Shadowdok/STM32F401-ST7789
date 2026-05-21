/* USER CODE BEGIN Header */
/**
 ******************************************************************************
  * @file    user_diskio.c
  * @brief   This file includes a diskio driver skeleton to be completed by the user.
  ******************************************************************************
  */
/* USER CODE END Header */

#include "main.h"
#include <stdint.h>
#include <string.h>
#include "ff_gen_drv.h"

// --- НАСТРОЙКИ ---
extern SPI_HandleTypeDef hspi2; // Твой аппаратный SPI2

#define SD_CS_LOW()    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_RESET)
#define SD_CS_HIGH()   HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_SET)

// --- ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ ---

// Надежный и быстрый обмен байтом через регистры (с защитой от мусора на F4)
uint8_t SPI_Transmit(uint8_t data) {
    // 1. Ждем, пока SPI освободится от прошлых задач
    while (__HAL_SPI_GET_FLAG(&hspi2, SPI_FLAG_BSY));

    // 2. Сбрасываем флаг переполнения и очищаем старый мусор из буфера приема
    __HAL_SPI_CLEAR_OVRFLAG(&hspi2);
    volatile uint32_t dummy = hspi2.Instance->DR;
    (void)dummy;

    // 3. Пишем байт напрямую в регистр данных
    *(__IO uint8_t *)&hspi2.Instance->DR = data;

    // 4. Ждем, пока придет ответ от карты
    while (!__HAL_SPI_GET_FLAG(&hspi2, SPI_FLAG_RXNE));

    // 5. Возвращаем чистый байт от карты
    return *(__IO uint8_t *)&hspi2.Instance->DR;
}

// Отправка команд карте памяти
uint8_t SD_SendCmd(uint8_t cmd, uint32_t arg) {
    uint8_t res, n;

    SD_CS_HIGH();
    SPI_Transmit(0xFF);

    SD_CS_LOW();
    for(volatile int d = 0; d < 100; d++); // Железная пауза для Cortex-M4

    SPI_Transmit(0xFF);

    SPI_Transmit(cmd | 0x40);
    SPI_Transmit((uint8_t)(arg >> 24));
    SPI_Transmit((uint8_t)(arg >> 16));
    SPI_Transmit((uint8_t)(arg >> 8));
    SPI_Transmit((uint8_t)arg);

    uint8_t crc = 0x01;
    if (cmd == 0) crc = 0x95;
    if (cmd == 8) crc = 0x87;
    SPI_Transmit(crc);

    n = 10;
    do {
        res = SPI_Transmit(0xFF);
    } while ((res & 0x80) && --n);

    return res;
}

// --- ОСНОВНЫЕ ФУНКЦИИ ДРАЙВЕРА ---

DSTATUS USER_initialize (BYTE pdrv) {
    uint8_t res, n;
    HAL_Delay(300); // Увеличенная задержка для полной зарядки карты

    SD_CS_HIGH();
    for(n = 0; n < 30; n++) SPI_Transmit(0xFF); // Шлем пустые такты

    // Сброс карты (CMD0)
    for(n = 0; n < 150; n++) {
        res = SD_SendCmd(0, 0);
        if (res == 1) break;
        HAL_Delay(5);
    }

    if (res != 1) return STA_NOINIT;

    // Проверка типа карты (CMD8)
    res = SD_SendCmd(8, 0x1AA);
    if (res == 1) { // Современные SDHC/SDXC карты
        for (n = 0; n < 4; n++) SPI_Transmit(0xFF);

        // Мощный цикл ожидания готовности (ACMD41) с паузами по 5мс
        uint16_t timeout = 3000;
        do {
            SD_SendCmd(55, 0);
            res = SD_SendCmd(41, 0x40000000);
            HAL_Delay(5);
        } while (res != 0 && --timeout);

        if (timeout == 0) return STA_NOINIT;
    } else { // Старые SD v1.x карты
        uint16_t timeout = 3000;
        do {
            SD_SendCmd(55, 0);
            res = SD_SendCmd(41, 0);
            HAL_Delay(5);
        } while (res != 0 && --timeout);

        if (timeout == 0) return STA_NOINIT;
    }

    return 0; // Инициализация успешна!
}

DSTATUS USER_status (BYTE pdrv) {
    return 0;
}

DRESULT USER_read (BYTE pdrv, BYTE *buff, DWORD sector, UINT count) {
    for (; count > 0; count--, sector++, buff += 512) {
        if (SD_SendCmd(17, sector) != 0) return RES_ERROR;

        uint16_t timeout = 0xFFFF;
        while (SPI_Transmit(0xFF) != 0xFE && --timeout);
        if (timeout == 0) return RES_ERROR;

        // Прямое чтение из регистра (максимальная скорость на F4)
        for (int i = 0; i < 512; i++) {
            while (!(hspi2.Instance->SR & SPI_SR_TXE));
            *(__IO uint8_t *)&hspi2.Instance->DR = 0xFF;
            while (!(hspi2.Instance->SR & SPI_SR_RXNE));
            buff[i] = *(__IO uint8_t *)&hspi2.Instance->DR;
        }

        SPI_Transmit(0xFF);
        SPI_Transmit(0xFF);
    }
    SD_CS_HIGH();
    SPI_Transmit(0xFF);
    return RES_OK;
}

// Структура драйвера для FatFS
Diskio_drvTypeDef USER_Driver = {
  USER_initialize,
  USER_status,
  USER_read,
#if _USE_WRITE
  NULL,
#endif
#if _USE_IOCTL
  NULL,
#endif
};
