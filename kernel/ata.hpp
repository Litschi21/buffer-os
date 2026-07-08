#pragma once

#include "kernel.hpp"
#include <stdint.h>

bool ata_init();
bool ata_read (uint8_t drive, uint64_t lba, void *buf, uint16_t sectors);
bool ata_write(uint8_t drive, uint64_t lba, void *buf, uint16_t sectors);
