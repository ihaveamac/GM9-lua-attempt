make
$DEVKITARM/bin/arm-none-eabi-objcopy -O binary arm9/arm9.elf --only-section=AHBWRAM AHBWRAM.bin
$DEVKITARM/bin/arm-none-eabi-objcopy -O binary arm9/arm9.elf --only-section=AHBWRAM2 AHBWRAM2.bin
firmtool build out.firm -D AHBWRAM.bin AHBWRAM2.bin arm11/arm11.elf -A 0x08000000 0x080a0000 -C NDMA NDMA XDMA -n 0x08000040
