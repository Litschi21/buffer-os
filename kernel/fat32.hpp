#pragma once

#include "kernel.hpp"
#include <stdint.h>

struct __attribute__((packed)) PART_Entry {
	uint64_t boot_ind:8;        // Boot Indicator, 0x80 = active, 0x00 = inactive
	uint64_t CHS_addr_start:24; // CHS Address of Partition Start
	uint64_t PART_type:8;       // Partition Type
	uint64_t CHS_addr_last:24;  // CHS Address of Last Partition

	uint32_t LBA_start;         // LBA of Partition Start
	uint32_t sec_count;         // Sector Count
};

struct __attribute__((packed)) MBR {
	uint8_t    bootstrap[446]; 
	PART_Entry entries[4];     // Partition entries 1 - 4
	uint16_t   boot_sig;       // MUST be 0x55AA, because IBM chose that magic number
};

struct __attribute__((packed)) FSInfo {
	uint32_t lead_sig;       // Must be 0x41615252 ???
	uint8_t  res0[480];      // 480 reserved bytes
	uint32_t struct_sig;     // Must be 0x61417272, at least we stick to the pattern
	uint32_t free_cls_cnt;   // Free cluster count, if 0xFFFFFFFF it's empty, otherwise
						     // check if it's <= volume cluster count
	uint32_t free_cls_start; // Start of where to check for free cluster
	uint8_t  res1[12];
	uint32_t trail_sig;      // 0xAA550000
};

struct __attribute__((packed)) BPB {
	uint8_t  asm_instr[3];    // Assembly instructions for jumping to boot part
	uint64_t oem_ident;       // Original Equipment Manufacturer Identifier
	uint16_t bytes_per_sec;   // Bytes per Sector
	uint8_t  sec_per_cls;     // Sector per Cluster
	uint16_t res_sec;         // Reserved Sectors
	uint8_t  num_FAT;         // Number of FATs
	uint16_t num_rootdir_ent; // Num of Entries in the Root Directory
	uint16_t sec_cnt_16;      // Sector Count 16 Bytes, go to 32 Byte version if set to 0
							  // for some fucking reason, because apparently we can't just use
							  // the 32 byte to begin with, probably due to FAT12 and 16, but
							  // like, we could just use 1 struct for FAT32, but sure ig
	uint8_t  media_desc_type; // Media Description Type
	uint16_t sec_per_FAT_16;  // Sectors per FAT
	uint16_t sec_per_trk;     // Sectors per Track
	uint16_t num_heads;       // Number of Heads/Sides
	uint32_t num_hidd_sec;    // Number of Hidden Sectors
	uint32_t sec_cnt_32;      // Sector Count 32 Bytes

	uint32_t sec_per_FAT_32;  // Sectors per FAT
	uint16_t flags;           // Flags, why are you even checking the comment for this
	uint16_t ver;             // FAT version
	uint32_t cls_num;
	uint16_t FSInfo_sec_num;  // Sector Number of FSInfo struct
	uint16_t backup_sec_num;  // Sector Number of Backup Boot Sector
	uint8_t  res0[12];        // Should be 0 after formatting
	uint8_t  drive_num;       // Drive Number
	uint8_t  res1;            // Reserved, except for Windows NT, but no one uses that
	uint8_t  sig;             // Must be 0x28 or 0x29
	uint32_t vol_id_num;      // Volume ID Serial Number
	uint8_t  vol_lbl_str[11]; // Volume Label String
	uint8_t  sys_id_str[8];   // Always "FAT32 ", never trust this, apparently
	uint8_t  boot_code[420];  // Boot code
	uint16_t boot_part_sig;   // 0xAA55
};

struct __attribute__((packed)) FAT32_Info {
	uint32_t LBA_start;
	uint64_t FAT_start;
	uint64_t FAT_table;
	uint64_t data_start;
	FSInfo fsinfo;
	BPB bpb;
};

struct __attribute__((packed)) DIR_Entry {
	uint8_t  filename[8];
	uint8_t  file_ext[3];
	uint8_t  attr;
	uint8_t  res0;
	uint8_t  crt_tenths;
	uint16_t crt_time;
	uint16_t crt_date;
	uint16_t last_accessed;
	uint16_t first_cls_high;
	uint16_t last_write_time;
	uint16_t last_write_date;
	uint16_t first_cls_low;
	uint32_t file_size;
};

extern FAT32_Info partitions[4];

bool check_entry(PART_Entry entry);
void fat32_init();
void read_dir(uint64_t cls);
