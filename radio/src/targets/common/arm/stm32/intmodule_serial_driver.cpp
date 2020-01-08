/*
 * Copyright (C) OpenTX
 *
 * Based on code named
 *   th9x - http://code.google.com/p/th9x
 *   er9x - http://code.google.com/p/er9x
 *   gruvin9x - http://code.google.com/p/gruvin9x
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "opentx.h"

ModuleFifo intmoduleFifo;

void intmoduleStop()
{
  GPIO_ResetBits(INTMODULE_PWR_GPIO, INTMODULE_PWR_GPIO_PIN);

  INTMODULE_DMA_STREAM->CR &= ~DMA_SxCR_EN; // Disable DMA

  GPIO_InitTypeDef GPIO_InitStructure;
  GPIO_InitStructure.GPIO_Pin = INTMODULE_TX_GPIO_PIN | INTMODULE_RX_GPIO_PIN;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
  GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
  GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_DOWN;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz;
  GPIO_Init(INTMODULE_GPIO, &GPIO_InitStructure);

  USART_DeInit(INTMODULE_USART);

  GPIO_ResetBits(INTMODULE_GPIO, INTMODULE_TX_GPIO_PIN | INTMODULE_RX_GPIO_PIN);

#if defined(INTERNAL_MODULE_MULTI)
  // stop pulses timer
  INTMODULE_TIMER->DIER &= ~TIM_DIER_CC2IE;
  INTMODULE_TIMER->CR1 &= ~TIM_CR1_CEN;
#endif
}

void intmodulePxx1SerialStart()
{
  intmoduleSerialStart(INTMODULE_PXX1_SERIAL_BAUDRATE, false, USART_Parity_No, USART_StopBits_1, USART_WordLength_8b);
}

void intmoduleSerialStart(uint32_t baudrate, uint8_t rxEnable, uint16_t parity, uint16_t stopBits, uint16_t wordLength)
{
  INTERNAL_MODULE_ON();

  NVIC_InitTypeDef NVIC_InitStructure;
  NVIC_InitStructure.NVIC_IRQChannel = INTMODULE_DMA_STREAM_IRQ;
  NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
  NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0; /* Not used as 4 bits are used for the pre-emption priority. */;
  NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
  NVIC_Init(&NVIC_InitStructure);

  GPIO_PinAFConfig(INTMODULE_GPIO, INTMODULE_GPIO_PinSource_TX, INTMODULE_GPIO_AF);
  GPIO_PinAFConfig(INTMODULE_GPIO, INTMODULE_GPIO_PinSource_RX, INTMODULE_GPIO_AF);

  GPIO_InitTypeDef GPIO_InitStructure;
  GPIO_InitStructure.GPIO_Pin = INTMODULE_TX_GPIO_PIN | INTMODULE_RX_GPIO_PIN;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
  GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
  GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz;
  GPIO_Init(INTMODULE_GPIO, &GPIO_InitStructure);

  USART_DeInit(INTMODULE_USART);
  USART_InitTypeDef USART_InitStructure;
  USART_InitStructure.USART_BaudRate = baudrate;
  USART_InitStructure.USART_Parity = parity;
  USART_InitStructure.USART_StopBits = stopBits;
  USART_InitStructure.USART_WordLength = wordLength;
  USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
  USART_InitStructure.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
  USART_Init(INTMODULE_USART, &USART_InitStructure);
  USART_Cmd(INTMODULE_USART, ENABLE);

  if (rxEnable) {
    intmoduleFifo.clear();
    USART_ITConfig(INTMODULE_USART, USART_IT_RXNE, ENABLE);
    NVIC_SetPriority(INTMODULE_USART_IRQn, 6);
    NVIC_EnableIRQ(INTMODULE_USART_IRQn);
  }
}

#if defined(INTERNAL_MODULE_MULTI) || defined(INTMODULE_TIMER)
void intmoduleTimerStart(uint32_t periodMs)
{
  // Timer
  INTMODULE_TIMER->CR1 &= ~TIM_CR1_CEN;
  INTMODULE_TIMER->PSC = INTMODULE_TIMER_FREQ / 2000000 - 1; // 0.5uS (2Mhz)
  INTMODULE_TIMER->ARR = periodMs * 2000;
  INTMODULE_TIMER->CCR2 = (periodMs - 1) * 2000;
  INTMODULE_TIMER->CCER = TIM_CCER_CC3E;
  INTMODULE_TIMER->CCMR2 = 0;
  INTMODULE_TIMER->EGR = 1; // Restart

  INTMODULE_TIMER->CCMR2 = TIM_CCMR2_OC3M_1 | TIM_CCMR2_OC3M_0; // Toggle CC1 o/p
  INTMODULE_TIMER->SR &= ~TIM_SR_CC2IF; // Clear flag
  INTMODULE_TIMER->DIER |= TIM_DIER_CC2IE;  // Enable this interrupt
  INTMODULE_TIMER->CR1 |= TIM_CR1_CEN;

  NVIC_EnableIRQ(INTMODULE_TIMER_IRQn);
  NVIC_SetPriority(INTMODULE_TIMER_IRQn, 7);
}
#endif

#define USART_FLAG_ERRORS (USART_FLAG_ORE | USART_FLAG_NE | USART_FLAG_FE | USART_FLAG_PE)
extern "C" void INTMODULE_USART_IRQHandler(void)
{
  uint32_t status = INTMODULE_USART->SR;

  while (status & (USART_FLAG_RXNE | USART_FLAG_ERRORS)) {
    uint8_t data = INTMODULE_USART->DR;
    if (status & USART_FLAG_ERRORS) {
      intmoduleFifo.errors++;
    }
    else {
      intmoduleFifo.push(data);
    }
    status = INTMODULE_USART->SR;
  }
}

void intmoduleSendByte(uint8_t byte)
{
  while (!(INTMODULE_USART->SR & USART_SR_TXE));
  USART_SendData(INTMODULE_USART, byte);
}

void intmoduleSendBuffer(const uint8_t * data, uint8_t size)
{
  if (size == 0)
    return;

  DMA_InitTypeDef DMA_InitStructure;
  DMA_DeInit(INTMODULE_DMA_STREAM);
  DMA_InitStructure.DMA_Channel = INTMODULE_DMA_CHANNEL;
  DMA_InitStructure.DMA_PeripheralBaseAddr = CONVERT_PTR_UINT(&INTMODULE_USART->DR);
  DMA_InitStructure.DMA_DIR = DMA_DIR_MemoryToPeripheral;
  DMA_InitStructure.DMA_Memory0BaseAddr = CONVERT_PTR_UINT(data);
  DMA_InitStructure.DMA_BufferSize = size;
  DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
  DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
  DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
  DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
  DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;
  DMA_InitStructure.DMA_Priority = DMA_Priority_VeryHigh;
  DMA_InitStructure.DMA_FIFOMode = DMA_FIFOMode_Disable;
  DMA_InitStructure.DMA_FIFOThreshold = DMA_FIFOThreshold_Full;
  DMA_InitStructure.DMA_MemoryBurst = DMA_MemoryBurst_Single;
  DMA_InitStructure.DMA_PeripheralBurst = DMA_PeripheralBurst_Single;
  DMA_Init(INTMODULE_DMA_STREAM, &DMA_InitStructure);
  DMA_Cmd(INTMODULE_DMA_STREAM, ENABLE);
  USART_DMACmd(INTMODULE_USART, USART_DMAReq_Tx, ENABLE);
}

void intmoduleSendNextFrame()
{
  switch(moduleState[INTERNAL_MODULE].protocol) {
#if defined(PXX2)
    case PROTOCOL_CHANNELS_PXX2_HIGHSPEED:
      intmoduleSendBuffer(intmodulePulsesData.pxx2.getData(), intmodulePulsesData.pxx2.getSize());
      break;
#endif

#if defined(PXX1)
    case PROTOCOL_CHANNELS_PXX1_SERIAL:
      intmoduleSendBuffer(intmodulePulsesData.pxx_uart.getData(), intmodulePulsesData.pxx_uart.getSize());
      break;
#endif

#if defined(INTERNAL_MODULE_MULTI)
    case PROTOCOL_CHANNELS_MULTIMODULE:
      intmoduleSendBuffer(intmodulePulsesData.multi.getData(), intmodulePulsesData.multi.getSize());
      break;
#endif
  }
}

#if defined(INTERNAL_MODULE_MULTI)
extern "C" void INTMODULE_TIMER_IRQHandler()
{
  INTMODULE_TIMER->SR &= ~TIM_SR_CC2IF;           // clear flag
  setupPulsesInternalModule();
  intmoduleSendNextFrame();
}
#endif
