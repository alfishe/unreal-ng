#pragma once
#include "stdafx.h"

unsigned wd93_crc(uint8_t *ptr, unsigned size);
uint16_t crc16(uint8_t *buf, unsigned size);
void crc32(int &crc, uint8_t *buf, unsigned len);
