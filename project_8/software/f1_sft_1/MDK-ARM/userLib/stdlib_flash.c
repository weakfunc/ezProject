#include "stdlib_flash.h"
#include "stm32f1xx_hal.h"
#include <string.h>

/* Flash 页内布局（共 1024 字节）：
 *   偏移 0x000: 魔术字    uint32_t (4 字节)  0xA55A1234
 *   偏移 0x004: 数据体    flashStore_t（含 version 字段，由调用方维护）
 */

/* 魔术字在页内的字节偏移。 */
#define FLASH_OFFSET_MAGIC      (0U)
/* 数据体在页内的字节偏移。 */
#define FLASH_OFFSET_DATA       (4U)

/* 上次 Init 是否读到有效数据：1=有效，0=使用了默认值。 */
static uint8_t flashDataValid = 0U;

/* 公有存储区，所有模块直接读写。 */
flashStore_t flashStore;

/* 从 Flash 地址读取一个 uint32_t。 */
static uint32_t __FLASH_ReadU32(uint32_t addr){
  uint32_t val;

  memcpy(&val, (const void *)addr, sizeof(uint32_t));
  return val;
}

/* 将内存缓冲区逐半字写入 Flash，buf_len 为字节数（奇数时末尾补零）。
 * 调用前须确保目标页已被擦除，且 HAL_FLASH_Unlock() 已完成。 */
static HAL_StatusTypeDef __FLASH_WriteHalfWords(uint32_t addr, const uint8_t *buf, uint32_t buf_len){
  uint32_t i;          /* 字节索引，步进 2 */
  uint16_t hw;         /* 当前半字数据 */
  HAL_StatusTypeDef ret;

  for(i = 0U; i < buf_len; i += 2U){
    /* 最后一字节为奇数尾时，高字节补零。 */
    if((i + 1U) < buf_len){
      hw = (uint16_t)buf[i] | ((uint16_t)buf[i + 1U] << 8U);
    } else {
      hw = (uint16_t)buf[i];
    }

    ret = HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, addr + i, (uint64_t)hw);
    if(ret != HAL_OK){
      return ret;
    }
  }

  return HAL_OK;
}

/* 将 value 按半字写入 Flash 指定地址（addr 须半字对齐）。 */
static HAL_StatusTypeDef __FLASH_WriteU32(uint32_t addr, uint32_t value){
  uint8_t buf[4];

  buf[0] = (uint8_t)(value        & 0xFFU);
  buf[1] = (uint8_t)((value >> 8U) & 0xFFU);
  buf[2] = (uint8_t)((value >> 16U) & 0xFFU);
  buf[3] = (uint8_t)((value >> 24U) & 0xFFU);

  return __FLASH_WriteHalfWords(addr, buf, 4U);
}

/* 从 Flash 加载数据到 flashStore；魔术字不匹配则全零并调一次 Save。 */
void STDLIB_FLASH_Init(void){
  uint32_t magic;  /* 读取的魔术字 */

  magic = __FLASH_ReadU32(USER_FLASH_STORE_PAGE_ADDR + FLASH_OFFSET_MAGIC);

  if(magic == USER_FLASH_MAGIC_WORD){
    /* 魔术字匹配，直接从 Flash 拷贝数据体（含 version 字段）。 */
    memcpy(&flashStore,
           (const void *)(USER_FLASH_STORE_PAGE_ADDR + FLASH_OFFSET_DATA),
           sizeof(flashStore_t));
    flashDataValid = 1U;
  } else {
    /* 数据无效，清零后写入 Flash 作为初始默认值。 */
    memset(&flashStore, 0, sizeof(flashStore_t));
    flashDataValid = 0U;
    STDLIB_FLASH_Save();
  }
}

/* 将当前 flashStore 写入 Flash（擦页 + 逐半字写入魔术字、size、数据体）。 */
void STDLIB_FLASH_Save(void){
  FLASH_EraseInitTypeDef eraseInit;  /* 擦除配置 */
  uint32_t               pageError;  /* 擦除错误页地址 */

  eraseInit.TypeErase   = FLASH_TYPEERASE_PAGES;
  eraseInit.PageAddress = USER_FLASH_STORE_PAGE_ADDR;
  eraseInit.NbPages     = 1U;

  HAL_FLASH_Unlock();

  /* 擦除整页。 */
  if(HAL_FLASHEx_Erase(&eraseInit, &pageError) != HAL_OK){
    HAL_FLASH_Lock();
    return;
  }

  /* 写入魔术字。 */
  if(__FLASH_WriteU32(USER_FLASH_STORE_PAGE_ADDR + FLASH_OFFSET_MAGIC, USER_FLASH_MAGIC_WORD) != HAL_OK){
    HAL_FLASH_Lock();
    return;
  }

  /* 写入数据体。 */
  __FLASH_WriteHalfWords(USER_FLASH_STORE_PAGE_ADDR + FLASH_OFFSET_DATA,
                         (const uint8_t *)&flashStore,
                         (uint32_t)sizeof(flashStore_t));

  HAL_FLASH_Lock();
}

/* flashStore 全零后写入 Flash。 */
void STDLIB_FLASH_Reset(void){
  memset(&flashStore, 0, sizeof(flashStore_t));
  STDLIB_FLASH_Save();
}

/* 返回上次 Init 的数据有效性：1=有效，0=使用了默认值。 */
uint8_t STDLIB_FLASH_IsValid(void){
  return flashDataValid;
}
