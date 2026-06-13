/// @file pins.hpp
/// @brief GPIO pin assignments for the custom PCB.
/// @details Pinout for ESP32-S3FH4R2 on the branch monitoring sensor board.
/// Defines pins for LSM6DS3 I2C, microSD SDIO/SPI, UART, acoustic emission
/// sensor (GPIO and ADC), and SD detect/battery monitor.
/// @ingroup main
///
/// @par Pin Assignment Table
/// | Signal       | GPIO | Function               |
/// |--------------|------|------------------------|
/// | IMU_SDA      |   5  | I2C Data               |
/// | IMU_SCL      |   6  | I2C Clock              |
/// | SD_CLK       |  12  | SDIO/SPI Clock         |
/// | SD_CMD_DI    |  11  | SDIO CMD / SPI MOSI    |
/// | SD_DAT0_DO   |  13  | SDIO DAT0 / SPI MISO   |
/// | SD_DAT3_CS   |  10  | SPI Chip Select        |
/// | SD_DET       |   9  | Card detect             |
/// | AE_ADC_PIN   |   1  | ADC1_CH0 analog input   |
/// | AE_GPIO_PIN  |  15  | Digital interrupt input |
/// | TX_PIN       |  43  | Debug UART TX           |
/// | RX_PIN       |  44  | Debug UART RX           |

#include <driver/gpio.h>

namespace pins {
  constexpr gpio_num_t TX_PIN = GPIO_NUM_43;
  constexpr gpio_num_t RX_PIN = GPIO_NUM_44;

  constexpr gpio_num_t GP1_PIN = GPIO_NUM_1;
  constexpr gpio_num_t GP3_PIN = GPIO_NUM_3;
  constexpr gpio_num_t GP4_PIN = GPIO_NUM_4;
  constexpr gpio_num_t GP7_PIN = GPIO_NUM_7;
  constexpr gpio_num_t GP10_PIN = GPIO_NUM_10;

  constexpr gpio_num_t IMU_SDO_SA0 = GPIO_NUM_4;
  constexpr gpio_num_t IMU_SDA = GPIO_NUM_5;
  constexpr gpio_num_t IMU_SCL = GPIO_NUM_6;
  constexpr gpio_num_t IMU_CS = GPIO_NUM_7;
  constexpr gpio_num_t IMU_INT1 = GPIO_NUM_2;
  constexpr gpio_num_t IMU_INT2 = GPIO_NUM_3;

  constexpr gpio_num_t SD_DAT0_DO = GPIO_NUM_13;
  constexpr gpio_num_t SD_CLK = GPIO_NUM_12;
  constexpr gpio_num_t SD_CMD_DI = GPIO_NUM_11;
  constexpr gpio_num_t SD_DAT3_CS = GPIO_NUM_10;
  constexpr gpio_num_t SD_DET = GPIO_NUM_9;
  constexpr gpio_num_t SD_BATT = GPIO_NUM_8;

  constexpr gpio_num_t AE_ADC_PIN = GP1_PIN;  // ADC1_CH0
  constexpr gpio_num_t AE_GPIO_PIN = GPIO_NUM_15;
}