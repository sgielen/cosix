#!/bin/sh

SOURCEDIR="$1"
BINDIR="$2"
ISONAME="$3"

if [ ! -f "${SOURCEDIR}/misc/iso/menu.lst" ]; then
	echo "Failed to find menu.lst in sourcedir, are you calling this script correctly?" >&2
	exit 1
fi

if [ ! -f "${BINDIR}/cloudkernel" ]; then
	echo "Failed to find cloudkernel, are you calling this script correctly?" >&2
	exit 1
fi

if [ -z "${ISONAME}" ]; then
	ISONAME="cosix.iso"
fi

MKISOFS="$(which mkisofs)"
if [ ! -x "${MKISOFS}" ]; then
	MKISOFS="$(which genisoimage)"
	if [ ! -x "${MKISOFS}" ]; then
		MKISOFS="$(which xorriso)"
		if [ -x "${MKISOFS}" ]; then
			MKISOFS="${MKISOFS} -as mkisofs"
		else
			echo "Failed to find mkisofs/genisoimage/xorriso, install one of them" >&2
			exit 1
		fi
	fi
fi

if [ -f "${ISONAME}" ]; then
	rm ${ISONAME}
fi

TMPDIR="iso-tmp"

if [ -d "${TMPDIR}" ]; then
	rm -rf ${TMPDIR}
fi

mkdir -p $TMPDIR/boot/grub
cp ${SOURCEDIR}/misc/iso/menu.lst        ${TMPDIR}/boot/grub
cp ${SOURCEDIR}/misc/iso/stage2_eltorito ${TMPDIR}/boot/grub
cp ${BINDIR}/cloudkernel                  ${TMPDIR}/boot
${MKISOFS} -V "COSIX" \
	-o ${ISONAME} -iso-level 3 -R \
	-b boot/grub/stage2_eltorito -no-emul-boot \
	--boot-load-size 4 -boot-info-table \
	${TMPDIR}
if [ ! -f "${ISONAME}" ]; then
	echo "Generation of ${ISONAME} failed." >&2
	exit 1
fi
rm -rf "${TMPDIR}"
