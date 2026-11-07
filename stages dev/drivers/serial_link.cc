// Copyright 2017 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
// 
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// UART driver for agreeable conversations with the neighbors.

#include "stages/drivers/serial_link.h"
#include "stages/midi_config.h"

namespace stages {

struct DirectionDefinition {
  GPIO_TypeDef* gpio;
  uint16_t tx_rx_pins;
  uint16_t rx_pin_source;
  uint16_t tx_pin_source;

  USART_TypeDef* usart;

  DMA_Channel_TypeDef* tx_dma_channel;
  DMA_Channel_TypeDef* rx_dma_channel;
  uint32_t tx_complete_flag;
  uint32_t rx_complete_flag;
  uint32_t rx_half_complete_flag;
};

const DirectionDefinition direction_definition[] = {
  { GPIOB,
    GPIO_Pin_8 | GPIO_Pin_9,
    GPIO_PinSource9,
    GPIO_PinSource8,
    USART3,
    DMA1_Channel2,
    DMA1_Channel3,
    DMA1_FLAG_TC2,
    DMA1_FLAG_TC3,
    DMA1_FLAG_HT3,
  },
  
  { GPIOC,
    GPIO_Pin_4 | GPIO_Pin_5,
    GPIO_PinSource5,
    GPIO_PinSource4,
    USART1,
    DMA1_Channel4,
    DMA1_Channel5,
    DMA1_FLAG_TC4,
    DMA1_FLAG_TC5,
    DMA1_FLAG_HT5,
  }
}; 
  
void SerialLink::Init(
    SerialLinkDirection direction,
    uint32_t baud_rate,
    uint8_t* rx_buffer,
    size_t rx_block_size) {
  direction_ = direction;
  rx_buffer_ = rx_buffer;
  rx_block_size_ = rx_block_size;
  rx_tail_ = 0;
  tx_idle_ = true;
  
  // Initialize clocks.
  if (direction == SERIAL_LINK_DIRECTION_LEFT) {
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOB, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);
  } else {
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOC, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);
  }
  RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

  const DirectionDefinition& definition = direction_definition[direction];
  
  // Initialize GPIO.
  GPIO_InitTypeDef gpio_init;
  gpio_init.GPIO_Speed = GPIO_Speed_2MHz;
  
  if (direction == SERIAL_LINK_DIRECTION_RIGHT) {
    // PC4 = USART1_TX (AF7) -> Tip (Data out to Module 2)
    GPIO_PinAFConfig(GPIOC, GPIO_PinSource4, GPIO_AF_7);
    gpio_init.GPIO_Pin = GPIO_Pin_4;
    gpio_init.GPIO_Mode = GPIO_Mode_AF;
    gpio_init.GPIO_OType = GPIO_OType_PP;
    gpio_init.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_Init(GPIOC, &gpio_init);

    // PC5 = USART1_RX (AF7) -> Ring (Data in from Module 2)
    GPIO_PinAFConfig(GPIOC, GPIO_PinSource5, GPIO_AF_7);
    gpio_init.GPIO_Pin = GPIO_Pin_5;
    gpio_init.GPIO_Mode = GPIO_Mode_AF;
    gpio_init.GPIO_OType = GPIO_OType_PP;
    gpio_init.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_Init(GPIOC, &gpio_init);
  } else {
    // PB9 = USART3_RX (AF7) -> Tip (Data in from Module 1)
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource9, GPIO_AF_7);
    gpio_init.GPIO_Pin = GPIO_Pin_9;
    gpio_init.GPIO_Mode = GPIO_Mode_AF;
    gpio_init.GPIO_OType = GPIO_OType_PP;
    gpio_init.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_Init(GPIOB, &gpio_init);

    // PB8 = USART3_TX (AF7) -> Ring (Data out to Module 1 or Synth)
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource8, GPIO_AF_7);
    gpio_init.GPIO_Pin = GPIO_Pin_8;
    gpio_init.GPIO_Mode = GPIO_Mode_AF;
    gpio_init.GPIO_OType = GPIO_OType_PP;
    gpio_init.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_Init(GPIOB, &gpio_init);
  }
  
  // Initialize USART.
  USART_InitTypeDef usart_init;
  usart_init.USART_BaudRate = baud_rate;
  usart_init.USART_WordLength = USART_WordLength_8b;
  usart_init.USART_StopBits = USART_StopBits_1;
  usart_init.USART_Parity = USART_Parity_No;
  usart_init.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
  usart_init.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;

  USART_Init(definition.usart, &usart_init);
#if MIDI_INVERT_TX
  // Must be configured while the USART is disabled (UE = 0).
  USART_InvPinCmd(definition.usart, USART_InvPin_Tx, ENABLE);
#endif
  USART_Cmd(definition.usart, ENABLE);
  
  // Initialize DMA channels.
  DMA_InitTypeDef dma_init;
  dma_init.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
  dma_init.DMA_MemoryInc = DMA_MemoryInc_Enable;
  dma_init.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
  dma_init.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
  dma_init.DMA_Mode = DMA_Mode_Normal;
  dma_init.DMA_Priority = DMA_Priority_High;
  dma_init.DMA_M2M = DMA_M2M_Disable;
  dma_init.DMA_BufferSize = 0;
  dma_init.DMA_MemoryBaseAddr = 0;
  
  dma_init.DMA_PeripheralBaseAddr = (uint32_t)&(definition.usart->TDR);
  dma_init.DMA_DIR = DMA_DIR_PeripheralDST;
  DMA_Cmd(definition.tx_dma_channel, DISABLE);
  DMA_DeInit(definition.tx_dma_channel);
  DMA_Init(definition.tx_dma_channel, &dma_init);
  USART_DMACmd(definition.usart, USART_DMAReq_Tx, ENABLE);

  if (rx_block_size_) {
    dma_init.DMA_PeripheralBaseAddr = (uint32_t)&(definition.usart->RDR);
    dma_init.DMA_DIR = DMA_DIR_PeripheralSRC;
    dma_init.DMA_Mode = DMA_Mode_Circular;
    dma_init.DMA_MemoryBaseAddr = (uint32_t)rx_buffer_;
    dma_init.DMA_BufferSize = rx_block_size_;

    DMA_Cmd(definition.rx_dma_channel, DISABLE);
    DMA_DeInit(definition.rx_dma_channel);
    DMA_Init(definition.rx_dma_channel, &dma_init);
    DMA_Cmd(definition.rx_dma_channel, ENABLE);
    USART_DMACmd(definition.usart, USART_DMAReq_Rx, ENABLE);
  }
}

void SerialLink::Transmit(const void* buffer, size_t size) {
  const DirectionDefinition& definition = direction_definition[direction_];

  // Disable DMA channel.
  definition.tx_dma_channel->CCR &= (uint16_t)(~DMA_CCR_EN);
  
  // Clear DMA TC flag and USART TC flag before enabling.
  DMA1->IFCR = definition.tx_complete_flag;
  USART_ClearFlag(definition.usart, USART_FLAG_TC);

  // Update size and buffer pointer.
  definition.tx_dma_channel->CNDTR = size;
  definition.tx_dma_channel->CMAR = (uint32_t)(buffer);

  // Enable DMA channel.
  definition.tx_dma_channel->CCR |= DMA_CCR_EN;
  tx_idle_ = false;
}

bool SerialLink::tx_complete() {
  if (tx_idle_) {
    return true;
  }
  const DirectionDefinition& definition = direction_definition[direction_];
  if (DMA1->ISR & definition.tx_complete_flag) {
    if (USART_GetFlagStatus(definition.usart, USART_FLAG_TC) != RESET) {
      DMA1->IFCR = definition.tx_complete_flag;
      USART_ClearFlag(definition.usart, USART_FLAG_TC);
      tx_idle_ = true;
      return true;
    }
  }
  return false;
}

void SerialLink::Receive(void* buffer, size_t size) {
  const DirectionDefinition& definition = direction_definition[direction_];
  
  // Disable DMA channel.
  definition.rx_dma_channel->CCR &= (uint16_t)(~DMA_CCR_EN);

  // Update size and buffer pointer.
  definition.rx_dma_channel->CNDTR = size;
  definition.rx_dma_channel->CMAR = (uint32_t)(buffer);

  // Clear DMA and USART RXNE flag.
  // DMA1->IFCR = definition.rx_complete_flag;
  // definition.usart->ICR = USART_FLAG_RXNE;

  // Enable DMA channel.
  definition.rx_dma_channel->CCR |= DMA_CCR_EN;
}

bool SerialLink::rx_complete() {
  const DirectionDefinition& definition = direction_definition[direction_];
  return DMA1->ISR & definition.rx_complete_flag;
}

const uint8_t* SerialLink::available_rx_buffer() {
  const DirectionDefinition& definition = direction_definition[direction_];
  uint32_t status = DMA1->ISR;
  if (status & definition.rx_half_complete_flag) {
    DMA1->IFCR = definition.rx_half_complete_flag;
    return &rx_buffer_[0];
  } else if (status & definition.rx_complete_flag) {
    DMA1->IFCR = definition.rx_complete_flag;
    return &rx_buffer_[rx_block_size_];
  } else {
    return NULL;
  }
}

size_t SerialLink::available() const {
  if (!rx_buffer_ || rx_block_size_ == 0) return 0;
  const DirectionDefinition& definition = direction_definition[direction_];
  size_t dma_bytes_left = definition.rx_dma_channel->CNDTR;
  size_t write_pos = (dma_bytes_left <= rx_block_size_)
      ? (rx_block_size_ - dma_bytes_left)
      : 0;
  if (write_pos >= rx_block_size_) write_pos = 0;
  if (write_pos >= rx_tail_) {
    return write_pos - rx_tail_;
  } else {
    return rx_block_size_ - rx_tail_ + write_pos;
  }
}

uint8_t SerialLink::Read() {
  if (!rx_buffer_ || rx_block_size_ == 0) return 0;
  uint8_t b = rx_buffer_[rx_tail_];
  rx_tail_ = (rx_tail_ + 1) % rx_block_size_;
  return b;
}

void SerialLink::SetSwapped(bool swapped) {
  if (direction_ != SERIAL_LINK_DIRECTION_RIGHT) {
    return;
  }
  const DirectionDefinition& definition = direction_definition[direction_];
  
  USART_Cmd(definition.usart, DISABLE);
  
  GPIO_InitTypeDef gpio_init;
  gpio_init.GPIO_Speed = GPIO_Speed_2MHz;
  
  if (swapped) {
    // TRS Type B Output for Synth (Tip = +3.3V, Ring = TX)
    // PC4 (Tip) = +3.3V Power
    gpio_init.GPIO_Pin = GPIO_Pin_4;
    gpio_init.GPIO_Mode = GPIO_Mode_OUT;
    gpio_init.GPIO_OType = GPIO_OType_PP;
    gpio_init.GPIO_PuPd = GPIO_PuPd_NOPULL;
    GPIO_Init(GPIOC, &gpio_init);
    GPIO_SetBits(GPIOC, GPIO_Pin_4);

    // PC5 (Ring) = USART1_TX (AF7 via SWAP)
    GPIO_PinAFConfig(GPIOC, GPIO_PinSource5, GPIO_AF_7);
    gpio_init.GPIO_Pin = GPIO_Pin_5;
    gpio_init.GPIO_Mode = GPIO_Mode_AF;
    gpio_init.GPIO_OType = GPIO_OType_PP;
    gpio_init.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_Init(GPIOC, &gpio_init);

    USART_SWAPPinCmd(definition.usart, ENABLE);
  } else {
    // Inter-module Mode (PC4 = TX to Module 2, PC5 = RX from Module 2)
    GPIO_PinAFConfig(GPIOC, GPIO_PinSource4, GPIO_AF_7);
    gpio_init.GPIO_Pin = GPIO_Pin_4;
    gpio_init.GPIO_Mode = GPIO_Mode_AF;
    gpio_init.GPIO_OType = GPIO_OType_PP;
    gpio_init.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_Init(GPIOC, &gpio_init);

    GPIO_PinAFConfig(GPIOC, GPIO_PinSource5, GPIO_AF_7);
    gpio_init.GPIO_Pin = GPIO_Pin_5;
    gpio_init.GPIO_Mode = GPIO_Mode_AF;
    gpio_init.GPIO_OType = GPIO_OType_PP;
    gpio_init.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_Init(GPIOC, &gpio_init);

    USART_SWAPPinCmd(definition.usart, DISABLE);
  }

#if MIDI_INVERT_TX
  USART_InvPinCmd(definition.usart, USART_InvPin_Tx, ENABLE);
#endif

  USART_Cmd(definition.usart, ENABLE);
}

void SerialLink::SetPowerPinHigh() {
  if (direction_ == SERIAL_LINK_DIRECTION_LEFT) {
    // PB9 (Tip) = +3.3V Power for TRS Type B synth output
    GPIO_InitTypeDef gpio_init;
    gpio_init.GPIO_Speed = GPIO_Speed_2MHz;
    gpio_init.GPIO_Pin = GPIO_Pin_9;
    gpio_init.GPIO_Mode = GPIO_Mode_OUT;
    gpio_init.GPIO_OType = GPIO_OType_PP;
    gpio_init.GPIO_PuPd = GPIO_PuPd_NOPULL;
    GPIO_Init(GPIOB, &gpio_init);
    GPIO_SetBits(GPIOB, GPIO_Pin_9);
  }
}

}  // namespace stages
