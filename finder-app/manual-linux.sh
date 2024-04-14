#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.

set -e
set -u

OUTDIR=/tmp/aeld

KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.1.10
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-
NUM_CPU=$(/usr/bin/nproc)
HOST_SYSROOT=$(${CROSS_COMPILE}gcc -print-sysroot)

if [ $# -lt 1 ]
then
	echo "Using default directory ${OUTDIR} for output"
else
	OUTDIR=$1
	echo "Using passed directory ${OUTDIR} for output"
fi

mkdir -p ${OUTDIR}

if [ ! -d $OUTDIR ]; then
    echo "Could not create given directory: ${OUTDIR}"
    exit 1
fi

cd ${OUTDIR}

if [ ! -d "${OUTDIR}/linux-stable" ]; then
    #Clone only if the repository does not exist.
	echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
	git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION}
fi

if [ ! -e ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ]; then
    pushd ${OUTDIR}/linux-stable

    echo "Checking out version ${KERNEL_VERSION}"
    git checkout ${KERNEL_VERSION}

    # TODO: Add your kernel build steps here
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} mrproper
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} defconfig
    make -j${NUM_CPU} ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} all
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} dtbs

    popd    # back
fi

echo "Adding the Image in outdir"
cp ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ${OUTDIR}

echo "Creating the staging directory for the root filesystem"

if [ -d "${OUTDIR}/rootfs" ]
then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm -fR ${OUTDIR}/rootfs
fi

# TODO: Create necessary base directories
mkdir -p ${OUTDIR}/rootfs/bin ${OUTDIR}/rootfs/dev ${OUTDIR}/rootfs/etc ${OUTDIR}/rootfs/home
mkdir -p ${OUTDIR}/rootfs/lib ${OUTDIR}/rootfs/lib64 ${OUTDIR}/rootfs/proc ${OUTDIR}/rootfs/sbin
mkdir -p ${OUTDIR}/rootfs/sys ${OUTDIR}/rootfs/tmp ${OUTDIR}/rootfs/usr/bin ${OUTDIR}/rootfs/usr/lib
mkdir -p ${OUTDIR}/rootfs/usr/sbin ${OUTDIR}/rootfs/var/log

if [ ! -d "${OUTDIR}/busybox" ]
then
    git clone git://busybox.net/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
    # TODO:  Configure busybox
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} distclean
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} defconfig
else
    cd busybox
fi

# TODO: Make and install busybox
make -j${NUM_CPU} ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE}
make CONFIG_PREFIX=${OUTDIR}/rootfs ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} install

echo "Library dependencies"
INT_LIB=$(${CROSS_COMPILE}readelf -a ${OUTDIR}/rootfs/bin/busybox | grep "program interpreter")
SHR_LIB=$(${CROSS_COMPILE}readelf -a ${OUTDIR}/rootfs/bin/busybox | grep "Shared library")

echo "${INT_LIB}"
echo "${SHR_LIB}"

# TODO: Add library dependencies to rootfs
INT_LIB=$(echo ${INT_LIB::-1} | cut -d ' ' -f 4)

target=$(readlink -f ${HOST_SYSROOT}/${INT_LIB})

cp -P ${HOST_SYSROOT}/${INT_LIB} ${OUTDIR}/rootfs/lib/

if ! [ -z ${target+x} ]; then
    cp ${target} ${OUTDIR}/rootfs/lib64/
fi

for token in ${SHR_LIB}; do
    if [[ $token == "["* ]]; then
        lib=$(echo ${token::-1} | cut -d '[' -f 2)

        target=$(readlink -f ${HOST_SYSROOT}/lib64/${lib})
        cp -P ${HOST_SYSROOT}/lib64/${lib} ${OUTDIR}/rootfs/lib64/

        if ! [ -z ${target+x} ]; then
            cp ${target} ${OUTDIR}/rootfs/lib64/
        fi
    fi
done

# TODO: Make device nodes
cd ${OUTDIR}/rootfs/

sudo mknod -m 666 dev/null c 1 3
sudo mknod -m 600 dev/console c 5 1

# TODO: Clean and build the writer utility
cd ${FINDER_APP_DIR}

make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} clean
make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} writer

# TODO: Copy the finder related scripts and executables to the /home directory
# on the target rootfs
sudo cp -R ../conf ${OUTDIR}/rootfs
sudo cp -R * ${OUTDIR}/rootfs/home

#sudo cp *.sh ${OUTDIR}/rootfs/home
#sudo cp writer ${OUTDIR}/rootfs/home
#sudo cp -R conf ${OUTDIR}/rootfs/home

# TODO: Chown the root directory
cd ${OUTDIR}/rootfs
sudo chown -R root:root *

# TODO: Create initramfs.cpio.gz
find . | cpio -H newc -ov --owner root:root > ${OUTDIR}/initramfs.cpio

cd ${OUTDIR}
gzip -f initramfs.cpio
