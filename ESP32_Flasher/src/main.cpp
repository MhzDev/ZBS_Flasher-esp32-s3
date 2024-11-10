
/*   Autor: Aaron Christophel ATCnetz.de   */
#include <Arduino.h>
#include <stdio.h>
#include <stdint.h>
#include "zbs_interface.h"
#include "main.h"

#ifdef RGB_BUILTIN
#undef RGB_BUILTIN
#endif
#define RGB_BUILTIN 21

uint32_t FLASHER_VERSION = 0x00000020;

uint32_t spi_speed = 8000000; // Speed for hardware spi, default 8MHz can be set via the PC tool

uint16_t addr = 0;
void setup()
{
  Serial.begin(115200);

  pinMode(ZBS_RXD, INPUT);

  change_led();

  while (Serial.available()) // Flushing UART
    Serial.read();
}

void loop()
{
  UART_handler();
}

int UART_rx_state = 0;
uint8_t expected_len = 0;
long last_rx_time = 0;
uint8_t UART_rx_buffer[0x200] = {0};
int curr_data_pos = 0;
uint32_t CRC_calc = 0xAB34;
uint32_t CRC_in = 0;
uint8_t UART_CMD = 0;
void UART_handler()
{
  while (Serial.available())
  {
    last_rx_time = millis();
    uint8_t curr_char = Serial.read();
    switch (UART_rx_state)
    {
    case 0:
      if (curr_char == 'A') // Header
        UART_rx_state++;
      break;
    case 1:
      if (curr_char == 'T') // Header
        UART_rx_state++;
      else
        UART_rx_state = 0;
      break;
    case 2:
      UART_CMD = curr_char; // Receive current CMD
      CRC_calc = 0xAB34;
      CRC_calc += curr_char;
      UART_rx_state++;
      break;
    case 3:
      expected_len = curr_char; // Receive Expected length of data
      CRC_calc += curr_char;
      curr_data_pos = 0;
      if (expected_len == 0)
        UART_rx_state = 5;
      else
        UART_rx_state++;
      break;
    case 4:
      CRC_calc += curr_char; // Read the actual data
      UART_rx_buffer[curr_data_pos++] = curr_char;
      if (curr_data_pos == expected_len)
        UART_rx_state++;
      break;
    case 5:
      CRC_in = curr_char << 8; // Receive high byte of crude CRC
      UART_rx_state++;
      break;
    case 6:
      if ((CRC_calc & 0xffff) == (CRC_in | curr_char)) // Check if CRC is correct
      {
        /*
        Serial1.println("Uart_data_fully received");
        Serial1.printf("The CMD is: %02X , Len: %d\r\n", UART_CMD, expected_len);
        for (int i = 0; i < expected_len; i++)
        {
          Serial1.printf(" %02X", UART_rx_buffer[i]);
        }
        Serial1.printf("\r\n");*/
        change_led();
        handle_uart_cmd(UART_CMD, UART_rx_buffer, expected_len);
        UART_rx_state = 0;
      }
      break;

    default:
      break;
    }
  }
  if (UART_rx_state && (millis() - last_rx_time >= 10))
  {
    UART_rx_state = 0;
  }
}

typedef enum
{
  CMD_GET_VERSION = 1,
  CMD_RESET_ESP = 2,
  CMD_ZBS_BEGIN = 10,
  CMD_RESET_ZBS = 11,
  CMD_SELECT_PAGE = 12,
  CMD_SET_POWER = 13,
  CMD_READ_RAM = 20,
  CMD_WRITE_RAM = 21,
  CMD_READ_FLASH = 22,
  CMD_WRITE_FLASH = 23,
  CMD_READ_SFR = 24,
  CMD_WRITE_SFR = 25,
  CMD_ERASE_FLASH = 26,
  CMD_ERASE_INFOBLOCK = 27,
  CMD_SAVE_MAC_FROM_FW = 40,
  CMD_PASS_THROUGH = 50,
} ZBS_UART_PROTO;

uint8_t temp_buff[0x200] = {0};
void handle_uart_cmd(uint8_t cmd, uint8_t *cmd_buff, uint8_t len)
{
  switch (cmd)
  {
  case CMD_GET_VERSION:
    temp_buff[0] = FLASHER_VERSION >> 24;
    temp_buff[1] = FLASHER_VERSION >> 16;
    temp_buff[2] = FLASHER_VERSION >> 8;
    temp_buff[3] = FLASHER_VERSION;
    send_uart_answer(cmd, temp_buff, 4);
    break;
  case CMD_RESET_ESP:
    send_uart_answer(cmd, NULL, 0);
    delay(100);
    ESP.restart();
    break;
  case CMD_ZBS_BEGIN:
    if (cmd_buff[0] == 1)
    {
      spi_speed = 1000000;
    }
    else
    {
      spi_speed = 8000000;
    }
    temp_buff[0] = zbs.begin(ZBS_SS, ZBS_CLK, ZBS_MoSi, ZBS_MiSo, ZBS_Reset, ZBS_POWER, 0, spi_speed);
    send_uart_answer(cmd, temp_buff, 1);
    break;
  case CMD_RESET_ZBS:
    zbs.reset();
    temp_buff[0] = 1;
    send_uart_answer(cmd, temp_buff, 1);
    break;
  case CMD_SELECT_PAGE:
    temp_buff[0] = zbs.select_flash(cmd_buff[0] ? 1 : 0);
    send_uart_answer(cmd, temp_buff, 1);
    break;
  case CMD_SET_POWER:
    zbs.set_power(cmd_buff[0] ? 1 : 0);
    temp_buff[0] = 1;
    send_uart_answer(cmd, temp_buff, 1);
    break;
  case CMD_READ_RAM:
    temp_buff[0] = zbs.read_ram(cmd_buff[0]);
    send_uart_answer(cmd, temp_buff, 1);
    break;
  case CMD_WRITE_RAM:
    zbs.write_ram(cmd_buff[0], cmd_buff[1]);
    temp_buff[0] = 1;
    send_uart_answer(cmd, temp_buff, 1);
    break;
  case CMD_READ_FLASH:
    // cmd_buff[0] = len
    // cmd_buff[1] << 8 | cmd_buff[2] = position
    for (int i = 0; i < cmd_buff[0]; i++)
    {
      temp_buff[i] = zbs.read_flash((cmd_buff[1] << 8 | cmd_buff[2]) + i);
    }
    send_uart_answer(cmd, temp_buff, cmd_buff[0]);
    break;
  case CMD_WRITE_FLASH:
    // cmd_buff[0] = len
    // cmd_buff[1] << 8 | cmd_buff[2] = position
    // cmd_buff[3+i] = data
    if (cmd_buff[0] >= (0xff - 3))
    { // Len too high, only 0xFF - header len possible
      temp_buff[0] = 0xEE;
      send_uart_answer(cmd, temp_buff, 1);
      break;
    }
    for (int i = 0; i < cmd_buff[0]; i++)
    {
      if (cmd_buff[3 + i] != 0xff)
      {
        zbs.write_flash((cmd_buff[1] << 8 | cmd_buff[2]) + i, cmd_buff[3 + i]);
        if (zbs.read_flash((cmd_buff[1] << 8 | cmd_buff[2]) + i) != cmd_buff[3 + i])
        {
          zbs.write_flash((cmd_buff[1] << 8 | cmd_buff[2]) + i, cmd_buff[3 + i]);
          if (zbs.read_flash((cmd_buff[1] << 8 | cmd_buff[2]) + i) != cmd_buff[3 + i])
          {
            zbs.write_flash((cmd_buff[1] << 8 | cmd_buff[2]) + i, cmd_buff[3 + i]);
            if (zbs.read_flash((cmd_buff[1] << 8 | cmd_buff[2]) + i) != cmd_buff[3 + i])
            {
              zbs.write_flash((cmd_buff[1] << 8 | cmd_buff[2]) + i, cmd_buff[3 + i]);
              if (zbs.read_flash((cmd_buff[1] << 8 | cmd_buff[2]) + i) != cmd_buff[3 + i])
              {
                temp_buff[0] = 0;
                send_uart_answer(cmd, temp_buff, 1);
                break;
              }
            }
          }
        }
      }
    }
    temp_buff[0] = 1;
    send_uart_answer(cmd, temp_buff, 1);
    break;
  case CMD_READ_SFR:
    temp_buff[0] = zbs.read_sfr(cmd_buff[0]);
    send_uart_answer(cmd, temp_buff, 1);
    break;
  case CMD_WRITE_SFR:
    zbs.write_sfr(cmd_buff[0], cmd_buff[1]);
    temp_buff[0] = 1;
    send_uart_answer(cmd, temp_buff, 1);
    break;
  case CMD_ERASE_FLASH:
    zbs.erase_flash();
    temp_buff[0] = 1;
    send_uart_answer(cmd, temp_buff, 1);
    break;
  case CMD_ERASE_INFOBLOCK:
    zbs.erase_infoblock();
    temp_buff[0] = 1;
    send_uart_answer(cmd, temp_buff, 1);
    break;
  case CMD_SAVE_MAC_FROM_FW:
  {
    uint8_t *temp = nullptr;
    zbs.select_flash(0);
    // get original mac (first few bytes) from stock firmware
    temp_buff[0] = 0;
    temp_buff[1] = 0;
    bool validMac = true;
    for (uint8_t c = 0; c < 6; c++)
    {
      temp_buff[c + 2] = zbs.read_flash(0xFC06 + c);
    }
    // try to recognize device type by reading a part of the flash
    temp = (uint8_t *)calloc(8, 1);
    for (uint8_t c = 0; c < 8; c++)
    {
      temp[c] = zbs.read_flash(0x08 + c);
    }
    const uint8_t val29[8] = {0x7d, 0x22, 0xff, 0x02, 0xa4, 0x58, 0xf0, 0x90};
    const uint8_t val29_1[8] = {0x7d, 0x22, 0xff, 0x02, 0xaa, 0xb3, 0xf0, 0x90};
    const uint8_t val154[8] = {0xa1, 0x23, 0x22, 0x02, 0xa4, 0xc3, 0xe4, 0xf0};
    const uint8_t val42[8] = {0xDF, 0x22, 0x22, 0x02, 0xAD, 0x35, 0xAE, 0x04};
    if (memcmp(temp, val29, 8) == 0)
    {
      // 2.9" 033
      temp_buff[6] = 0x3B;
      temp_buff[7] = 0x10;
    }
    else if (memcmp(temp, val29_1, 8) == 0)
    {
      // 2.9" 033 as well
      temp_buff[6] = 0x3B;
      temp_buff[7] = 0x10;
    }
    else if (memcmp(temp, val154, 8) == 0)
    {
      // 1.54" 033
      temp_buff[6] = 0x34;
      temp_buff[7] = 0x10;
    }
    else if (memcmp(temp, val42, 8) == 0)
    {
      // 4.2" 033
      temp_buff[6] = 0x48;
      temp_buff[7] = 0x30;
    }
    else
    {
      // not supported...
      validMac = false; // can't assume the mac we read makes any sense...
      temp_buff[6] = 0xFF;
      temp_buff[7] = 0xFF;
    }
    free(temp);
    // calculate last nibble (checksum)
    uint8_t xorchk = 0;
    for (uint8_t c = 2; c < 8; c++)
    {
      xorchk ^= (temp_buff[c] & 0x0F);
      xorchk ^= (temp_buff[c] >> 4);
    }
    temp_buff[7] |= xorchk;

    // check if there's already a mac in the infoblock
    zbs.select_flash(1); // select infoblock
    bool infoblockEmpty = true;
    for (uint8_t c = 0; c < 8; c++)
    {
      uint8_t check = zbs.read_flash(0x10 + c);
      if (check != 0xFF)
        infoblockEmpty = false;
    }

    if (infoblockEmpty && validMac)
    {
      // succes!
      for (uint8_t c = 0; c < 8; c++)
      {
        // write mac directly to infoblock without erasing; the bytes should all be 0xFF anyway
        zbs.write_flash(0x17 - c, temp_buff[c]);
      }
      temp_buff[0] = 1;
      send_uart_answer(cmd, temp_buff, 1);
    }
    else if (!infoblockEmpty)
    {
      bool macAlreadyMatches = true;
      for (uint8_t c = 0; c < 8; c++)
      {
        // check if the values match
        if (temp_buff[c] != zbs.read_flash(0x17 - c))
          macAlreadyMatches = false;
      }
      if (macAlreadyMatches)
      {
        temp_buff[0] = 1;
        send_uart_answer(cmd, temp_buff, 1);
      }
      else
      {
        // fail
        temp_buff[0] = 0;
        send_uart_answer(cmd, temp_buff, 1);
      }
    }
    else
    {
      temp_buff[0] = 0;
      send_uart_answer(cmd, temp_buff, 1);
    }
    break;
  }
  case CMD_PASS_THROUGH:
#ifdef ESP32
    temp_buff[0] = 1;
    send_uart_answer(cmd, temp_buff, 1);
    Serial1.begin(115200, SERIAL_8N1, ZBS_RXD, ZBS_TXD);
    while (!Serial.available())
    {
      if (Serial1.available())
        Serial.write(Serial1.read());
    }
    Serial1.end();
    pinMode(ZBS_RXD, INPUT);
    digitalWrite(ZBS_RXD, LOW);
    pinMode(ZBS_TXD, INPUT);
    digitalWrite(ZBS_TXD, LOW);
#else // Only ESP32 Support the second Serial
    temp_buff[0] = 0;
    send_uart_answer(cmd, temp_buff, 1);
#endif
    break;
  }
}

uint8_t answer_buffer[0x200] = {0};
void send_uart_answer(uint8_t answer_cmd, uint8_t *ans_buff, uint8_t len)
{
  uint32_t CRC_value = 0xAB34;
  answer_buffer[0] = 'A';
  answer_buffer[1] = 'T';
  answer_buffer[2] = answer_cmd;
  CRC_value += answer_cmd;
  answer_buffer[3] = len;
  CRC_value += len;
  for (int i = 0; i < len; i++)
  {
    answer_buffer[4 + i] = ans_buff[i];
    CRC_value += ans_buff[i];
  }
  answer_buffer[2 + 2 + len] = CRC_value >> 8;
  answer_buffer[2 + 2 + len + 1] = CRC_value;
  Serial.write(answer_buffer, 2 + 2 + len + 2);
  /*
    Serial1.println("Uart_answer now sending");
    Serial1.printf("The CMD is: %02X , Len: %d\r\n", answer_cmd, len);
    for (int i = 0; i < (2 + 2 + len + 1); i++)
    {
      Serial1.printf(" %02X", answer_buffer[i]);
    }
    Serial1.printf("\r\n");*/
}

bool ledState = false;

void change_led() {
  if (ledState) {
    neopixelWrite(RGB_BUILTIN, 0, 0, 0);
  } else {
    neopixelWrite(RGB_BUILTIN, 255 * 0.05, 50 * 0.05, 180 * 0.05);
  }
  ledState = !ledState;
}