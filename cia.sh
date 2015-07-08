make
$DEVKITARM/bin/arm-none-eabi-strip music_player.elf
makerom -f cia -o music_player.cia -elf music_player.elf -rsf ./res/cia.rsf -icon ./res/icon.bin -banner ./res/banner.bin -exefslogo -target t
