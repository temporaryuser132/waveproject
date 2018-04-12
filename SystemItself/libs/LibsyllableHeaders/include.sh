#!/bin/bash

cd system

make clean

make -i

cd stage

cp image home/root/system/waveiso

zip -9 -r waveiso/Packages/basewavebase.zip stage/image

mkdir -p /home/root/Desktop/waveiso/Packages/base

mkisofs -iso-level 3 --allow-leading-dots -R -V "WaveCD" -b boot/grub/stage2_eltorito -no-emul-boot -boot-load-size 4 -boot-info-table -o /home/root/Desktop/WaveCD2.iso /home/root/system/waveiso