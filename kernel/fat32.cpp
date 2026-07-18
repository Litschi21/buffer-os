#include "ahci.hpp"
#include "fat32.hpp"
#include "kernel.hpp"
#include <stdint.h>

FAT32_Info partitions[4]{};

bool check_entry(PART_Entry entry) {
	if (entry.PART_type == 0x0B ||
		entry.PART_type == 0x0C)
		return true;

	return false;
}

void fat32_init() {
	MBR buf;
	if (!ahci_op(0, 0, &buf, 1, false)) {
		print("Error while attempting to read AHCI, unable to use file systems.\n");
		return;
	}

	if (buf.boot_sig != 0x55AA) {
		print("Invalid boot signature, unable to use file systems.");
		return;
	}
	
	for (uint8_t i{}; i < 4; ++i) {
		if (check_entry(buf.entries[i])) {
			BPB bpb;
			ahci_op(0, buf.entries[i].LBA_start, &bpb, 1, false);

			FSInfo fsinfo;
			ahci_op(0, buf.entries[i].LBA_start + bpb.FSInfo_sec_num, &fsinfo, 1, false);

			partitions[i].LBA_start = buf.entries[i].LBA_start;
			partitions[i].FAT_start = bpb.res_sec + buf.entries[i].LBA_start;
			partitions[i].fsinfo = fsinfo;
			partitions[i].bpb = bpb;

			uint64_t fat_table{ kmalloc(bpb.sec_per_FAT_32 * bpb.bytes_per_sec) };
			ahci_op(0, partitions[i].FAT_start, reinterpret_cast<void*>(fat_table),
					static_cast<uint64_t>(bpb.sec_per_FAT_32), false);
			
			partitions[i].FAT_table  = fat_table;
			partitions[i].data_start = partitions[i].LBA_start + (bpb.cls_num - 2) * bpb.sec_per_cls;
		}
	}
}
