#include "ahci.hpp"
#include "kernel.hpp"
#include <stdint.h>

void ahci_init(uint32_t bar5) {
	map_page(get_pml4(), VIRT_ADDR, bar5, PAGE_PRESENT | PAGE_WRITE);

	volatile HBA_MEM *abar{ reinterpret_cast<volatile HBA_MEM*>(VIRT_ADDR) };
	abar->ghc |= (1u << 31);
	abar->ghc |= 1;

	while (abar->ghc & 1);
	if (abar->pi == 0) {
		print("WARNING: No AHCI ports found, disk access not available!\n\n");
		return;
	}

	for (uint8_t i{}; i < 32; ++i) {
		if (!(abar->pi & (1u << i))) continue;	
	
		volatile HBA_PORT *port_ptr{ &abar->ports[i] };
		port_ptr->cmd &= ~(1u << 0); // Stop command list processing
		port_ptr->cmd &= ~(1u << 4); // Stop FIS receiver

		// Wait for FR and CR bits to clear
		while (port_ptr->cmd & (1u << 14) || port_ptr->cmd & (1u << 15));
		
		if (port_ptr->sig == 0x101) {
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
			if (port_ptr->tfd & (1u << 0)) return;

			// Send IDENTIFY cmd
			// Define command header
			HBA_CMD_HDR *cmd_hdr{ reinterpret_cast<HBA_CMD_HDR*>(clb_addr) };
			cmd_hdr->cfl      = sizeof(FIS_REG_H2D) / sizeof(uint32_t);
			cmd_hdr->w        = 0;
			cmd_hdr->prdtl    = 1;

			uint64_t cmdtbl_addr{ kmalloc(sizeof(HBA_CMD_TBL), 128) };
			HBA_CMD_TBL *cmdtbl { reinterpret_cast<HBA_CMD_TBL*>(cmdtbl_addr) };

			FIS_REG_H2D *fis    { reinterpret_cast<FIS_REG_H2D*>(cmdtbl->cfis) };
			fis->fis_type = 0x27; // Host to Device
			fis->command  = 0xEC; // IDENTIFY Device
			fis->c        = 1;    // Command instead of Control, just repeating what is already in .hpp atp
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

			drives[i].model[40] = '\0';
			drives[i].serial[20] = '\0';
			drives[i].firmware[8] = '\0';
		}
	}
}
