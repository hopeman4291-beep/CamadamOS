#!/bin/sh
# Repack grub-mkrescue's image as a plain MBR hybrid (no GPT / Apple partition
# map / HFS+), which Windows handles without hanging when the stick is plugged in.
set -e
IN=$1; OUT=$2; T=build/isotree
rm -rf $T; mkdir -p $T
xorriso -osirrox on -indev "$IN" -extract / $T >/dev/null 2>&1
chmod -R u+w $T
rm -rf $T/System $T/mach_kernel $T/boot.catalog
UUIDF=$(ls $T/.disk/*.uuid | head -1)
DATE=$(basename "$UUIDF" .uuid | tr -d '-')
EFI=$T/efi.img; mv $EFI build/efi.img
xorriso -as mkisofs -o "$OUT" -R -V CAMADAMOS --modification-date=$DATE \
  --grub2-mbr /usr/lib/grub/i386-pc/boot_hybrid.img \
  -partition_offset 16 --mbr-force-bootable \
  -append_partition 2 0xef build/efi.img \
  -c boot.catalog \
  -b boot/grub/i386-pc/eltorito.img -no-emul-boot -boot-load-size 4 -boot-info-table --grub2-boot-info \
  -eltorito-alt-boot -e --interval:appended_partition_2:all:: -no-emul-boot \
  $T >/dev/null 2>&1
