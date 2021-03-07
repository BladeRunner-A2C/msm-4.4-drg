#!/bin/bash
#
# Compile script for QuicksilveR kernel
# Copyright (C) 2020-2021 Adithya R.

SECONDS=0 # builtin bash timer
ZIPNAME="QuicksilveR-drg-$(date '+%Y%m%d-%H%M').zip"
if test -z "$(git rev-parse --show-cdup 2>/dev/null)" &&
   head=$(git rev-parse --verify HEAD 2>/dev/null); then
        ZIPNAME="${ZIPNAME::-4}-$(echo $head | cut -c1-8).zip"
fi
TC_DIR="$HOME/tc/clang-r416183b1"
GCC_64_DIR="$HOME/tc/aarch64-linux-android-4.9"
GCC_32_DIR="$HOME/tc/arm-linux-androideabi-4.9"
DEFCONFIG="vendor/drg-perf_defconfig"

export KBUILD_BUILD_USER=007
export KBUILD_BUILD_HOST=skyfall
export PATH="$TC_DIR/bin:$PATH"

if [[ $1 = "-c" || $1 = "--clean" ]]; then
	rm -rf out
fi

if [[ $1 = "-r" || $1 = "--regen" ]]; then
	make O=out ARCH=arm64 $DEFCONFIG savedefconfig
	cp out/defconfig arch/arm64/configs/$DEFCONFIG
	echo -e "\nSuccessfully regenerated defconfig at $DEFCONFIG"
	exit 1
fi

mkdir -p out
make O=out ARCH=arm64 $DEFCONFIG

echo -e "\nStarting compilation...\n"
make -j"$(nproc --all)" O=out ARCH=arm64 CC=clang CLANG_TRIPLE=aarch64-linux-gnu- CROSS_COMPILE=$GCC_64_DIR/bin/aarch64-linux-android- CROSS_COMPILE_ARM32=$GCC_32_DIR/bin/arm-linux-androideabi- Image.gz-dtb

if [ -f "out/arch/arm64/boot/Image.gz-dtb" ]; then
	echo -e "\nKernel compiled succesfully! Zipping up...\n"
	if ! git clone -q https://github.com/BladeRunner-A2C/AnyKernel3 -b drg; then
		echo -e "\nCloning AnyKernel3 repo failed! Aborting..."
		exit 1
	fi
	cp out/arch/arm64/boot/Image.gz-dtb AnyKernel3
	rm -f ./*zip
	cd AnyKernel3 || exit
	rm -rf out/arch/arm64/boot
	zip -r9 "../$ZIPNAME" ./* -x '*.git*' README.md ./*placeholder
	cd ..
	rm -rf AnyKernel3
	echo -e "\nCompleted in $((SECONDS / 60)) minute(s) and $((SECONDS % 60)) second(s) !"
	echo "Zip: $ZIPNAME"
	curl -# -F "file=@${ZIPNAME}" https://0x0.st
	echo
else
	echo -e "\nCompilation failed!"
fi
