CXX  = x86_64-elf-g++
AS   = x86_64-elf-as
NASM = nasm
LD   = x86_64-elf-ld

KERN = kernel/boot.o kernel/gdt_flush.o kernel/isr_exc.o kernel/remap_pic.o \
	   kernel/timer_isr.o kernel/keyboard_isr.o kernel/switch_ctx.o \
	   kernel/syscall_entry.o kernel/elf.o kernel/ata.o kernel/ahci.o \
	   kernel/fat32.o kernel/kernel.o
CXXFLAGS = -std=c++23 -ffreestanding -fno-exceptions -fno-rtti -mno-red-zone \
		   -mgeneral-regs-only -O2 -Wall -Wextra -Wpedantic -Wshadow  \
		   -Wold-style-cast -Wnull-dereference -Wformat=2 \
		   -Wno-misleading-indentation -Werror
LDFLAGS  = -n -T kernel/linker.ld

all: iso

kernel/boot.o: kernel/boot.s
	$(AS) kernel/boot.s -o kernel/boot.o

kernel/%.o: kernel/%.s
	$(NASM) -f elf64 $< -o $@

kernel/ahci.o: kernel/ahci.cpp
	$(CXX) -c kernel/ahci.cpp $(CXXFLAGS) -o kernel/ahci.o

kernel/ata.o: kernel/ata.cpp
	$(CXX) -c kernel/ata.cpp $(CXXFLAGS) -o kernel/ata.o

kernel/elf.o: kernel/elf.cpp
	$(CXX) -c kernel/elf.cpp $(CXXFLAGS) -o kernel/elf.o

kernel/fat32.o: kernel/fat32.cpp
	$(CXX) -c kernel/fat32.cpp $(CXXFLAGS) -o kernel/fat32.o

kernel/kernel.o: kernel/kernel.cpp
	$(CXX) -c kernel/kernel.cpp $(CXXFLAGS) -o kernel/kernel.o

buffer.bin: $(KERN)
	$(LD) $(LDFLAGS) -o $@ $(KERN)

iso: buffer.bin
	mkdir -p iso/boot/grub
	cp buffer.bin iso/boot/buffer.bin
	grub-mkrescue -o buffer.iso iso

run: iso
	qemu-img create -f raw disk.img 64M
	mkfs.fat -F 32 disk.img

	qemu-system-x86_64 -drive file=disk.img,if=none,id=disk,format=raw \
		-device ide-hd,drive=disk,bus=ide.0 -cdrom buffer.iso \
		-machine q35 -boot d

clean:
	rm -f kernel/*.o buffer.bin buffer.iso iso/boot/buffer.bin

.PHONY: all iso run clean
