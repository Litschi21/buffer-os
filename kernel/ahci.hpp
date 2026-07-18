#pragma once

#include "kernel.hpp"
#include <stdint.h>

#define VIRT_ADDR 0xFFFFF80000000000

struct FIS_REG_H2D {
	uint8_t fis_type;

	uint8_t pmport:4;   // Port mult
	uint8_t rsv0:3;     // Reserved
	uint8_t c:1;        // cmd = 1, ctl = 0
	
	uint8_t command;    // Command reg
	uint8_t featurel;   // Feature reg
	
	uint8_t lba0;       // LBA low reg
	uint8_t lba1;       // LBA mid reg
	uint8_t lba2;       // LBA high reg
	uint8_t device;     // Device reg

	uint8_t lba3;       // LBA reg
	uint8_t lba4;       // Same thing
	uint8_t lba5;       // You know it
	uint8_t featureh;   // Feature reg (another one, but high instead of low)

	uint8_t countl;     // Count reg (low)
	uint8_t counth;     // Count reg (high)
	uint8_t icc;        // Isochronous command completion
					    // Isochronous is a fire name for a dino, like a velociraptor-looking one
	uint8_t control;    // Control register

	uint8_t rsv1[4];    // Reserved
};

struct HBA_CMD_HDR {
	uint8_t  cfl:5;     // Command FIS len in DWORDS
	uint8_t  a:1;       // ATAPI
	uint8_t  w:1;       // Write, 1 = H2D, 0 = D2H
	uint8_t  p:1;       // Prefetchable (had to google that)

	uint8_t  r:1;       // Reset
	uint8_t  b:1;       // BIST
	uint8_t  c:1;       // Clear
	uint8_t  rsv0:1;    // Reserved
	uint8_t  pmp:4;     // Port multiplier port

	uint16_t prdtl;     // Physical region descriptor table length in entries
					    // These names are fucking ridiculous
	
	volatile
	uint32_t prdbc;     // Physical region descriptor byte count
	
	uint32_t ctba;      // Command table descriptor base address
	uint32_t ctbau;     // upper bits, you know the drill atp

	uint32_t rsv1[4];   // Reserved
};

struct HBA_PRDT_ENTRY {
	uint32_t dba;       // Data base addr
	uint32_t dbau;      // Upper part
	
	uint32_t rsv0;      // Reserved, once again
	
	uint32_t dbc:22;    // Byte count, max 4 MB
	uint32_t rsv1:9;    // Reserved
	uint32_t i:1;       // Interrupt on completion
};

struct HBA_CMD_TBL {
	uint8_t cfis[64];   // Command FIS
	uint8_t acmd[16];   // ATAPI command
	uint8_t  rsv[48];   // Reserved
	
	HBA_PRDT_ENTRY prdt_entry[1]; // Physical region descriptor table entries
								  // I am NOT moving all those comments so they align with this one
};

struct HBA_PORT {
	uint32_t clb;		// 0x00, command list base addr
	uint32_t clbu;		// 0x04, command list base addr upper 32 bits
	uint32_t fb;		// 0x08, FIS base addr
	uint32_t fbu;		// 0x0C, FIS base addr upper 32 bits
	uint32_t is;		// 0x10, interrupt status
	uint32_t ie;		// 0x14, interrupt enable
	uint32_t cmd;		// 0x18, command and status
	uint32_t rsv0;		// 0x1C, Reserved
	uint32_t tfd;		// 0x20, task file data
	uint32_t sig;		// 0x24, signature
	uint32_t ssts;		// 0x28, SATA status
	uint32_t sctl;		// 0x2C, SATA ctl
	uint32_t serr;		// 0x30, SATA error
	uint32_t sact;		// 0x34, SATA active
	uint32_t ci;		// 0x38, command issue
	uint32_t sntf;		// 0x3C, SATA notification
	uint32_t fbs;		// 0x40, FIS-based switch ctl
	uint32_t rsv1[11];	// Reserved
	uint32_t vendor[4];	// vendor specific
};

struct HBA_MEM {
	uint32_t cap;       // 0x00, capabilities
	uint32_t ghc;       // 0x04, global host control
	uint32_t is;        // 0x08, interrupt status
	uint32_t pi;        // 0x0C, port implemented
	uint32_t vs;        // 0x10, version
	uint32_t ccc_ctl;   // 0x14, command completion coalescing control
	uint32_t ccc_pts;   // 0x18, command completion coalescing ports, what the fuck are these names
	uint32_t em_loc;    // 0x1C, enclosure management location
	uint32_t em_ctl;    // 0x20, enclosure management ctl
	uint32_t cap2;      // 0x24, host capabilities extended
	uint32_t bohc;      // 0x28, BIOS/OS handoff control and status

	uint8_t  rsv[0xA0 - 0x2C];  // Reserved
	uint8_t  ven[0x100 - 0xA0]; // vendor specific regs
	HBA_PORT ports[1];          // port ctl regs
};

extern HBA_CMD_TBL *cmdtbl;

bool ahci_init (uint32_t bar5);
bool ahci_op (uint8_t drive, uint64_t lba, void *buf, uint64_t count, bool write);
