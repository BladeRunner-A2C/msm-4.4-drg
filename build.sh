#!/bin/bash
#
# Compile script for QuicksilveR kernel
# Copyright (C) 2020-2023 Adithya R.

# Setup getopt.
long_opts="regen,clean,sdclang,homedir:,tcdir:"
getopt_cmd=$(getopt -o rcsh:t: --long "$long_opts" \
            -n $(basename $0) -- "$@") || \
            { echo -e "\nError: Getopt failed. Extra args\n"; exit 1;}

eval set -- "$getopt_cmd"

while true; do
    case "$1" in
        -r|--regen|r|regen) FLAG_REGEN_DEFCONFIG=y;;
        -c|--clean|c|clean) FLAG_CLEAN_BUILD=y;;
        -s|--sdclang|s|sdclang) FLAG_SDCLANG_BUILD=y;;
        -h|--homedir|h|homedir) HOME_DIR="$2"; shift;;
        -t|--tcdir|t|tcdir) TC_DIR="$2"; shift;;
        -o|--outdir|o|outdir) OUT_DIR="$2"; shift;;
        --) shift; break;;
    esac
    shift
done

# Setup HOME dir
if [ $HOME_DIR ]; then
    HOME_DIR=$HOME_DIR
else
    HOME_DIR=$HOME
fi
echo -e "HOME directory is at $HOME_DIR\n"

# Setup Toolchain dir
if [ $TC_DIR ]; then
    TC_DIR="$HOME_DIR/$TC_DIR"
else
    TC_DIR="$HOME_DIR/tc"
fi
echo -e "Toolchain directory is at $TC_DIR\n"

# Setup OUT dir
if [ $OUT_DIR ]; then
    OUT_DIR=$OUT_DIR
else
    OUT_DIR=out
fi
echo -e "Out directory is at $OUT_DIR\n"

SECONDS=0 # builtin bash timer
ZIPNAME="QuicksilveR-drg-$(date '+%Y%m%d-%H%M').zip"
if test -z "$(git rev-parse --show-cdup 2>/dev/null)" &&
   head=$(git rev-parse --verify HEAD 2>/dev/null); then
        ZIPNAME="${ZIPNAME::-4}-$(echo $head | cut -c1-8).zip"
fi

CLANG_DIR="$TC_DIR/clang-r450784d"
SDCLANG_DIR="$TC_DIR/sdclang-14/compiler"
AK3_DIR="$HOME/AnyKernel3"
DEFCONFIG="vendor/drg-perf_defconfig"

if [ "$FLAG_SDCLANG_BUILD" = 'y' ]; then
export PATH="$SDCLANG_DIR/bin:$PATH"
else
export PATH="$CLANG_DIR/bin:$PATH"
fi

MAKE_PARAMS="O=$OUT_DIR ARCH=arm64 CC=clang LLVM_IAS=1 \
	HOSTLD=ld.lld LD=ld.lld LD_ARM32=ld.lld AR=llvm-ar NM=llvm-nm \
	OBJCOPY=llvm-objcopy OBJDUMP=llvm-objdump STRIP=llvm-strip \
	CLANG_TRIPLE=aarch64-linux-gnu- \
	CROSS_COMPILE_ARM32=arm-eabi- \
	CROSS_COMPILE=$CLANG_DIR/bin/llvm- Image.gz-dtb"

if [ "$FLAG_SDCLANG_BUILD" = 'y' ]; then
MAKE_PARAMS+=" HOSTCC=$CLANG_DIR/bin/clang"
else
MAKE_PARAMS+=" HOSTCC=clang"
fi

export KBUILD_BUILD_USER=punisher
export KBUILD_BUILD_HOST=nidavellir

# Regenerate defconfig, if requested so
if [ "$FLAG_REGEN_DEFCONFIG" = 'y' ]; then
	 make O=out ARCH=arm64 $DEFCONFIG savedefconfig
	cp $OUT_DIR/defconfig arch/arm64/configs/$DEFCONFIG
	echo -e "\nSuccessfully regenerated defconfig at $DEFCONFIG"
	exit 1
fi

# Prep for a clean build, if requested so
if [ "$FLAG_CLEAN_BUILD" = 'y' ]; then
	echo -e "\nCleaning output folder..."
	rm -rf $OUT_DIR
fi

mkdir -p $OUT_DIR
make O=$OUT_DIR ARCH=arm64 $DEFCONFIG

echo -e "\nStarting compilation...\n"
make -j"$(nproc --all)" $MAKE_PARAMS 2> >(tee error.log >&2) || exit $?

if [ -f "$OUT_DIR/arch/arm64/boot/Image.gz-dtb" ]; then
	echo -e "\nKernel compiled succesfully! Zipping up...\n"
        if [ -d "$AK3_DIR" ]; then
		cp -r $AK3_DIR AnyKernel3
		git -C AnyKernel3 checkout drg &> /dev/null
	elif ! git clone -q https://github.com/BladeRunner-A2C/AnyKernel3 -b drg; then
		echo -e "\nAnyKernel3 repo not found locally and couldn't clone from GitHub! Aborting..."
		exit 1
	fi
	cp $OUT_DIR/arch/arm64/boot/Image.gz-dtb AnyKernel3
	rm -f ./*zip
	cd AnyKernel3 || exit
	rm -rf $OUT_DIR/arch/arm64/boot
	zip -r9 "../$ZIPNAME" ./* -x '*.git*' README.md ./*placeholder
	cd ..
	rm -rf AnyKernel3
	echo -e "\nCompleted in $((SECONDS / 60)) minute(s) and $((SECONDS % 60)) second(s) !"
	echo "Zip: $ZIPNAME"
	curl -F "file=@${ZIPNAME}" https://oshi.at
	echo
else
	echo -e "\nCompilation failed!"
fi
