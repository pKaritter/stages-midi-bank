@echo off
echo Building Behringer Steps / MI Stages Firmware...
wsl bash -c "cd '/mnt/d/Open Source Audio Projects/MI Stages' && make -f 'stages/makefile' BUILD_ROOT=/tmp/build/ && /usr/bin/arm-none-eabi-objcopy -O binary /tmp/build/stages/stages.elf /tmp/build/stages/stages.bin && python3 stm_audio_bootloader/qpsk/encoder.py -t stm32f3 -k -s 48000 -b 12000 -c 6000 -p 256 /tmp/build/stages/stages.bin && cp /tmp/build/stages/stages.bin 'stages dev/build/' && cp /tmp/build/stages/stages.hex 'stages dev/build/' && cp /tmp/build/stages/stages.wav 'stages dev/build/'"
echo Done! Output files are located in build\
pause

