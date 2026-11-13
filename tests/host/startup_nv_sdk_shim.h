#pragma once
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include "host_types.h"
#define _attribute_packed_ __attribute__((packed))
#define _attribute_ram_code_
#define FLASH_ADDR_OF_MAC_ADDR_512K 0x76000u
#define FLASH_ADDR_OF_F_CFG_INFO_512K 0x77000u
#define CFG_FACTORY_RST_CNT 0x78000u
#define min2(a,b) ((a)<(b)?(a):(b))
#define ZB_EXCEPTION_POST(n) assert(0 && "SDK NV exception")
#include "drivers/drv_nv.h"
unsigned int xcrc32(const unsigned char *,int,unsigned int);
void flash_read(u32,u32,u8 *);
void flash_write(u32,u32,u8 *);
bool flash_writeWithCheck(u32,u32,u8 *);
void flash_erase(u32);
static inline void *ev_buf_allocate(unsigned n){return malloc(n);}
static inline void ev_buf_free(void *p){free(p);}
