#!/bin/sh

SFDISK=$(which sfdisk)
FDISK=$(which fdisk)
QEMU_IMG=$(which qemu-img)
GENEXT2FS=$(which genext2fs)
DD=$(which dd)

# Create a disk image with partition table
${QEMU_IMG} create -f raw disk.img 100M
if [ ! -z "${SFDISK}" ]; then
	# Use sfdisk
	${SFDISK} disk.img <<EOF
label: dos
label-id: 0x00000000
device: disk.img
unit: sectors

disk.img1 : start=           1, size=      204799, type=83
EOF
else
	# Assume fdisk from Mac OS
	${FDISK} -y -r disk.img <<EOF
1,190000,0x83,*,0,1,1,1023,254,63
0,0,0x00,-,0,0,0,0,0,0
0,0,0x00,-,0,0,0,0,0,0
0,0,0x00,-,0,0,0,0,0,0
EOF
fi

${GENEXT2FS} -b 95000 -z fs.img
${DD} if=fs.img of=disk.img seek=1
rm fs.img
