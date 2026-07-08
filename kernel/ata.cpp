#include "ata.hpp"
#include "kernel.hpp"
#include <stdint.h>

bool ata_init() {
	uint16_t base{ 0x1F0 };

	outb(base + 6, 0xA0); // Check 0x1F6, in case you can't do math
	outb(base + 7, 0xEC); // Send IDENTIFY

	if (inb(base + 7) == 0xFF) return false;

	uint8_t status{ inb(base + 7) };
	while (status & 0x80) // 0x80 = Slow ass drive still busy
		status = inb(base + 7);
	
	if (status & 0x08) { // 0x08 = good, drive info found
		// uint16_t buf[256];
		// for (uint16_t i{}; i < 256; ++i)
		// 	buf[i] = inw(base);
		
		return true;
	}

	return false;
}

bool ata_read(uint8_t drive, uint64_t lba, void *buf, uint16_t sectors) {
	uint16_t base{ 0x1F0 };

	outb(base + 6, 0x40 | (drive << 4) | ((lba >> 24) & 0x0F));
	outb(base + 2, sectors);

	outb(base + 3, lba);
	outb(base + 4, lba >> 8);
	outb(base + 5, lba >> 16);

	outb(base + 7, 0x20);
	for (uint16_t sec{}; sec < sectors; ++sec) {
		while (inb(base + 7) & 0x80); // Again, waiting on slow, busy drive
		if    (inb(base + 7) & 0x01) return false;
		
		for (uint16_t i{}; i < 256; ++i)
			reinterpret_cast<uint16_t*>(buf)[i] = inw(base);

		buf = reinterpret_cast<uint8_t*>(buf) + 512;
	}
	
	return true;
}

bool ata_write(uint8_t drive, uint64_t lba, void *buf, uint16_t sectors) {
	uint16_t base{ 0x1F0 };

	outb(base + 6, 0x40 | (drive << 4) | ((lba >> 24) & 0x0F));
	outb(base + 2, sectors);

	outb(base + 3, lba);
	outb(base + 4, lba >> 8);
	outb(base + 5, lba >> 16);

	outb(base + 7, 0x30);
	for (uint16_t sec{}; sec < sectors; ++sec) {
		while (inb(base + 7) & 0x80);
		if (inb(base + 7) & 0x01) return false;
		
		for (uint16_t i{}; i < 256; ++i)
			outw(base, reinterpret_cast<uint16_t*>(buf)[i]);
		
		buf = reinterpret_cast<uint8_t*>(buf) + 512;
	}

	while (inb(base + 7) & 0x80); // For last disk sector to not corrupt data
	return true;
}
