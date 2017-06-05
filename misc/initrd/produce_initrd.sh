#!/bin/sh

# Call this as: produce_initrd.sh initrd bin1 bin2 bin3 --into lib lib1 lib2 lib3
# Copies bin1, bin2 and bin3 into initrd /bin
# Copies lib1, lib2 and lib3 into initrd /lib
# Creates initrd

INITRDNAME="$1"
shift

TAR="$(which gtar)"
if [ ! -x "${TAR}" ]; then
	TAR="$(which tar)"
	if [ ! -x "${TAR}" ]; then
		echo "Failed to find gtar/tar, install one of them" >&2
		exit 1
	fi
fi

if [ -f "${INITRDNAME}" ]; then
	rm ${INITRDNAME}
fi

TMPDIR="initrd-tmp"
if [ -d "${TMPDIR}" ]; then
	rm -rf ${TMPDIR}
fi

INTO="bin"
mkdir -p $TMPDIR/$INTO

while (( "$#" )); do
	if [ "$1" == "--into" ]; then
		INTO="$2"
		mkdir -p $TMPDIR/$INTO
		shift
	else
		cp -R $1 "$TMPDIR/$INTO"
	fi
	shift
done

pushd ${TMPDIR}
${TAR} -cf initrd bin lib
popd
mv ${TMPDIR}/initrd ${INITRDNAME}

rm -rf ${TMPDIR}
