#include "ahci.hpp"
#include "kernel.hpp"
#include <stdint.h>

static volatile HBA_MEM *abar;
HBA_CMD_HDR *cmd_hdr;
HBA_CMD_TBL *cmdtbl;

bool ahci_init(uint32_t bar5) {
	bool drive_init{ false };
	map_page(get_pml4(), VIRT_ADDR, bar5, PAGE_PRESENT | PAGE_WRITE);

	abar = reinterpret_cast<volatile HBA_MEM*>(VIRT_ADDR);
	abar->ghc |= (1u << 31);
	abar->ghc |= 1;

	uint64_t start{ timer_ticks };
	while (abar->ghc & 1 && timer_ticks - start < PIT_FREQ / 20);

	if (abar->pi == 0) {
		print("WARNING: No AHCI ports found, disk access not available!\n\n");
		return false;
	}

	for (uint8_t i{}; i < 32; ++i) {
		if (!(abar->pi & (1u << i))) continue;
	
		volatile HBA_PORT *port_ptr{ &abar->ports[i] };
		port_ptr->cmd &= ~(1u << 0); // Stop command list processing
		port_ptr->cmd &= ~(1u << 4); // Stop FIS receiver

		// Wait for FR and CR bits to clear
		start = timer_ticks;
		while ((port_ptr->cmd & (1u << 14) || port_ptr->cmd & (1u << 15)) && timer_ticks - start < PIT_FREQ / 20);

		// Perform COMRESET
		port_ptr->sctl = 1;
		sleep(5);
		port_ptr->sctl = 0;

		start = timer_ticks;
		while ((port_ptr->ssts & 0xF) != 0x3 && timer_ticks - start < PIT_FREQ / 20);
		port_ptr->is = port_ptr->is;

		if (port_ptr->sig == 0x101) {
			drive_init = true;

			// Alloc CLB and FB
			uint64_t clb_addr{ kmalloc(1024, 1024) };
			memset(reinterpret_cast<void*>(clb_addr), 0, 1024);
			port_ptr->clb  = clb_addr & 0xFFFFFFFF; // AND with 32 bits of 1s to get only first 32 bits
			port_ptr->clbu = clb_addr >> 32;        // Move over 32 bits to get the upper 32 bits

			uint64_t fb_addr{ kmalloc(256, 256) };
			port_ptr->fb   = fb_addr & 0xFFFFFFFF;  // Same thing again
			port_ptr->fbu  = fb_addr >> 32;

			// Enable FRE and ST
			port_ptr->cmd |= (1u << 4);
			port_ptr->cmd |= (1u << 0);

			// Enable Interrupts
			port_ptr->ie  |= (1u << 0);

			// Check TFD, bit 7 = busy, bit 3 = data request, bit 0 = err
			while (port_ptr->tfd & (1u << 7) || port_ptr->tfd & (1u << 3));
			if (port_ptr->tfd & (1u << 0)) return false;

			// Send IDENTIFY cmd
			// Define command header
			cmd_hdr = reinterpret_cast<HBA_CMD_HDR*>(clb_addr);
			cmd_hdr->cfl      = sizeof(FIS_REG_H2D) / sizeof(uint32_t);
			cmd_hdr->w        = 0;
			cmd_hdr->prdtl    = 1;

			uint64_t cmdtbl_addr{ kmalloc(sizeof(HBA_CMD_TBL), 128)};
			cmdtbl = reinterpret_cast<HBA_CMD_TBL*>(cmdtbl_addr);

			FIS_REG_H2D *fis    { reinterpret_cast<FIS_REG_H2D*>(cmdtbl->cfis) };
			fis->fis_type = 0x27; // Host to Device
			fis->command  = 0xEC; // IDENTIFY Device
			fis->c        = 1;    // Command instead of Control, just repeating the names in .hpp atp
			fis->device   = 0;

			uint64_t buf_addr{ kmalloc(512) };
			cmdtbl->prdt_entry[0].dba  = buf_addr & 0xFFFFFFFF;
			cmdtbl->prdt_entry[0].dbau = buf_addr >> 32;
			cmdtbl->prdt_entry[0].dbc  = 511;
			cmdtbl->prdt_entry[0].i    = 1;

			cmd_hdr->ctba  = cmdtbl_addr & 0xFFFFFFFF;
			cmd_hdr->ctbau = cmdtbl_addr >> 32;

			memset(reinterpret_cast<void*>(buf_addr), 0, 512);

			port_ptr->ci  |= (1u << 0);
			while (port_ptr->ci & (1u << 0));

			port_ptr->is = port_ptr->is;

			uint16_t *id{ reinterpret_cast<uint16_t*>(buf_addr) };
			drives[i].sectors     = id[60] | (static_cast<uint64_t>(id[61]) << 16);
			drives[i].sector_size = 512;
			drives[i].lba48       = (id[83] >> 10) & 1;

			for (uint8_t j{}; j < 20; ++j) {
				drives[i].model[j * 2]     = (id[27 + j] >> 8) & 0xFF;
				drives[i].model[j * 2 + 1] = id[27 + j] & 0xFF;
			}

			for (uint8_t j{}; j < 10; ++j) {
				drives[i].serial[j * 2]     = (id[10 + j] >> 8) & 0xFF;
				drives[i].serial[j * 2 + 1] = id[10 + j] & 0xFF;
			}

			for (uint8_t j{}; j < 4; ++j) {
				drives[i].firmware[j * 2]     = (id[23 + j] >> 8) & 0xFF;
				drives[i].firmware[j * 2 + 1] = id[23 + j] & 0xFF;
			}

			drives[i].model[40]   = '\0';
			drives[i].serial[20]  = '\0';
			drives[i].firmware[8] = '\0';
		}
	}
	
	return drive_init;
}

bool ahci_op(uint8_t drive, uint64_t lba, void *buf, uint64_t count, bool write) {
	if (count == 0) return true;

	for (uint64_t sec{}; sec < count; ++sec) {
		// Clear Interrupt Status
		abar->ports[drive].is = 0xFFFFFFFF;

		// Set command header fields
		cmd_hdr->cfl   = sizeof(FIS_REG_H2D) / 4;
		cmd_hdr->w     = write ? 1 : 0;
		cmd_hdr->prdtl = 1;

		// Set up PRDT entry
		cmdtbl->prdt_entry[0].dba  = reinterpret_cast<uint64_t>(buf) & 0xFFFFFFFF;
		cmdtbl->prdt_entry[0].dbau = reinterpret_cast<uint64_t>(buf) >> 32;
		cmdtbl->prdt_entry[0].dbc  = 511;
		cmdtbl->prdt_entry[0].i    = 1;

		// Build H2H FIS
		FIS_REG_H2D *fis{ reinterpret_cast<FIS_REG_H2D*>(cmdtbl->cfis) };
		fis->fis_type = 0x27;
		fis->c        = 1;
		fis->command  = write ? 0x35 : 0x25;

		fis->lba0 = (lba >> 0) & 0xFF;
		fis->lba1 = (lba >> 8) & 0xFF;
		fis->lba2 = (lba >> 16) & 0xFF;
		fis->lba3 = (lba >> 24) & 0xFF;
		fis->lba4 = (lba >> 32) & 0xFF;
		fis->lba5 = (lba >> 40) & 0xFF;

		fis->countl   = 1;
		fis->counth   = 0;
		fis->device   = (1 << 6);

		abar->ports[drive].ci |= (1u << 0);
		while (abar->ports[drive].ci);

		if (abar->ports[drive].tfd & (1u << 0)) return false;
		abar->ports[drive].is = abar->ports[drive].is;

		buf = reinterpret_cast<void*>(reinterpret_cast<uint64_t>(buf) + 512);
		++lba;
	}

	return true;
}
