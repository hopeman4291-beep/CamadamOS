CC      = gcc
CFLAGS  = -m32 -ffreestanding -fno-builtin -fno-stack-protector -fno-pie -fno-pic -nostdlib \
          -fno-tree-loop-distribute-patterns -mno-sse -mno-mmx -mno-80387 -mgeneral-regs-only \
          -O2 -Wall -Wextra -Wno-unused-parameter -Wno-misleading-indentation -Isrc
LDFLAGS = -m elf_i386 -T linker.ld -nostdlib
SRCS    = $(wildcard src/*.c)
OBJS    = $(patsubst src/%.c,build/%.o,$(SRCS)) build/boot.o
GRUBCFG = iso/boot/grub/grub.cfg

all: camadamos.iso

build/%.o: src/%.c src/kernel.h | build
	$(CC) $(CFLAGS) -c $< -o $@
build/boot.o: src/boot.asm | build
	nasm -f elf32 $< -o $@
build:
	mkdir -p build
build/camadamos.elf: $(OBJS) linker.ld
	ld $(LDFLAGS) -o $@ $(OBJS)

# 1) the image that gets installed on a hard disk (no installer inside)
build/install.img: build/camadamos.elf $(GRUBCFG) tools/repack.sh
	rm -rf build/inner && mkdir -p build/inner/boot/grub
	cp build/camadamos.elf build/inner/boot/camadamos.elf && cp $(GRUBCFG) build/inner/boot/grub/
	grub-mkrescue -o build/inner.iso build/inner 2>/dev/null
	sh tools/repack.sh build/inner.iso $@

# 2) the USB image: same system plus the install image as a GRUB module
camadamos.iso: build/install.img
	rm -rf build/outer && mkdir -p build/outer/boot/grub
	cp build/camadamos.elf build/outer/boot/camadamos.elf && cp build/install.img build/outer/boot/install.img
	sed 's|multiboot /boot/camadamos.elf\(.*\)|multiboot /boot/camadamos.elf\1\n    module /boot/install.img|' $(GRUBCFG) > build/outer/boot/grub/grub.cfg
	grub-mkrescue -o build/outer.iso build/outer 2>/dev/null
	sh tools/repack.sh build/outer.iso $@

run: camadamos.iso
	qemu-system-i386 -m 512 -cdrom camadamos.iso -nic user,model=e1000 -device intel-hda -device hda-output -audiodev pa,id=snd0
clean:
	rm -rf build camadamos.iso
