#!/bin/bash

set -e

cd $(dirname "${BASH_SOURCE[0]}")/..
mkdir -p cutter-deps && cd cutter-deps

DEPS_BASE_URL=https://github.com/karliss/cutter-deps/releases/download/qt6-test #TODO: replace before merging

if [ "$CUTTER_QT" == "5" ]; then
	DEPS_FILE_linux_x86_64=cutter-deps-linux-x86_64.tar.gz
	DEPS_SHA256_linux_x86_64=0721c85548bbcf31f6911cdb2227e5efb4a20c34262672d4cd2193db166b2f8c
	DEPS_BASE_URL=https://github.com/rizinorg/cutter-deps/releases/download/v15
else
	DEPS_FILE_linux_x86_64=cutter-deps-linux-x86_64.tar.gz
	DEPS_SHA256_linux_x86_64=be2496d6c8f3e9a16220e4f268f5e2e33b066932644b3a8574c11380bf05cfef
fi
echo $DEPS_SHA256_linux_x86_64

DEPS_FILE_macos_x86_64=cutter-deps-macos-x86_64.tar.gz
DEPS_SHA256_macos_x86_64=8c1e39733e123c109837260e9cf0927eb9f76e2e18c35819fc7b99a802a449b0

DEPS_FILE_macos_arm64=cutter-deps-macos-arm64.tar.gz
DEPS_SHA256_macos_arm64=e3f51568c7ef9ae6ffeca21ea0a050a5cea3920973150ff3391dbcd7411f8d20

DEPS_FILE_win_x86_64=cutter-deps-win-x86_64.tar.gz
DEPS_SHA256_win_x86_64=9509521eb893fbe41eb94326b48a402822909114aef7ec1a2f89638d09d28ee2


ARCH=x86_64
if [ "$OS" == "Windows_NT" ]; then
	PLATFORM=win
else
	UNAME_S="$(uname -s)"
	if [ "$UNAME_S" == "Linux" ]; then
		PLATFORM=linux
	elif [ "$UNAME_S" == "Darwin" ]; then
		PLATFORM=macos
		ARCH=$(uname -m)
	else
		echo "Unsupported Platform: uname -s => $UNAME_S, \$OS => $OS"
		exit 1
	fi
fi

DEPS_FILE=DEPS_FILE_${PLATFORM}_${ARCH}
DEPS_FILE=${!DEPS_FILE}
DEPS_SHA256=DEPS_SHA256_${PLATFORM}_${ARCH}
DEPS_SHA256=${!DEPS_SHA256}
DEPS_URL=${DEPS_BASE_URL}/${DEPS_FILE}

SHA256SUM=sha256sum
if ! command -v ${SHA256SUM} &> /dev/null; then
	SHA256SUM="shasum -a 256"
fi

curl -L "$DEPS_URL" -o "$DEPS_FILE" || exit 1
echo "$DEPS_SHA256  $DEPS_FILE" | ${SHA256SUM} -c - || exit 1

tar -xf "$DEPS_FILE" || exit 1

if [ -f relocate.sh ]; then
	./relocate.sh || exit 1
fi

