#!/bin/bash

set -euo pipefail

pwd
ls

system_deps=$1
image=$2

#export TZ=UTC
#ln -snf /usr/share/zoneinfo/$TZ /etc/localtime && echo $TZ > /etc/timezone

apt-get -y update

# latest git and cmake
export GIT_VERSION="git-2.36.1"
export CMAKE_VERSION="3.25.3"

apt-get -y install wget libcurl4-gnutls-dev libexpat1-dev gettext libz-dev libssl-dev build-essential

wget "https://www.kernel.org/pub/software/scm/git/$GIT_VERSION.tar.gz"
tar -zxf "$GIT_VERSION.tar.gz"
# build.
make -C "$GIT_VERSION" prefix=/usr install -j > "$GIT_VERSION/build.log"
# ensure git is installed.
git version
wget "https://github.com/Kitware/CMake/releases/download/v$CMAKE_VERSION/cmake-$CMAKE_VERSION-linux-x86_64.sh"
bash ./cmake-$CMAKE_VERSION-linux-x86_64.sh --skip-license --prefix=/usr
# ensure cmake is installed.
cmake --version
# cleanup dev environment.
rm -rf "$GIT_VERSION.tar.gz" "$GIT_VERSION" cmake-$CMAKE_VERSION-linux-x86_64.sh
unset CMAKE_VERSION
unset GIT_VERSION

apt-get -y install libgraphviz-dev \
    mesa-common-dev \
    libxkbcommon-x11-dev \
    ninja-build \
    python3-pip \
    curl \
    libpcre2-dev \
    libfuse2 \
    pkg-config

if [ "$image" = "ubuntu:18.04" ]; then
    # install additional packages needed for appimage
    apt-get -y install gcc-7 \
                        libglu1-mesa-dev \
                        freeglut3-dev \
                        mesa-common-dev \
                        libclang-8-dev \
                        llvm-8
    ln -s /usr/bin/llvm-config-8 /usr/bin/llvm-config
fi
if [ "$image" = "ubuntu:18.04" ] || [ "$image" = "ubuntu:20.04" ]; then
    # install additional packages needed for appimage
    apt-get -y install libxcb1-dev \
                        libxkbcommon-dev \
                        libxcb-*-dev \
                        libegl1
fi
if [ "$image" = "ubuntu:20.04" ] && [ "$system_deps" = "false" ]; then
    # install additional packages needed for appimage
    apt-get -y install libclang-11-dev \
                        llvm-11 \
                        libsm6 \

                        libwayland-dev  \
                        libgl1-mesa-dev
fi
if [ "$image" = "ubuntu:18.04" ] && [ "$system_deps" = "true" ]; then
    apt-get -y install qt5-default \
                        libqt5svg5-dev \
                        qttools5-dev \
                        qttools5-dev-tools
fi
if [ "$image" = "ubuntu:22.04" ]; then
    apt-get -y install libclang-12-dev \
                        llvm-12 \
                        qt6-base-dev \
                        qt6-tools-dev \
                        qt6-tools-dev-tools \
                        libqt6svg6-dev \
                        libqt6core5compat6-dev \
                        libqt6svgwidgets6 \
                        qt6-l10n-tools \
                        gcc-12 \
                        g++-12
fi

# https://github.com/rizinorg/cutter/runs/7170222817?check_suite_focus=true
python3 -m pip install meson==0.61.5
env | grep PACKAGE