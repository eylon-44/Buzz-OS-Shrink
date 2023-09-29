set architecture i8086
set disassembly-flavor intel

layout asm
layout reg

target remote localhost:26000

break *0x7c00
break *0x1000
continue

define hook-stop

# Translate the segment:offset into a physical address
printf "[%4x:%4x] ", $cs, $eip
x/i $cs*16+$eip

end