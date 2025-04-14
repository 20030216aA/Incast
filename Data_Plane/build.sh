#!/bin/bash

P4_NAME=incast_detect
SRC_PATH=/root/bf-sde-9.7.0/home/cxm/incast_detect
BUILD_PATH=${SRC_PATH}/build
is_run=0

if [ "$#" -eq 1 ]; then
  if [ "$1" = "run" ]; then
    is_run=1
  else
    echo 'usage: ./build.sh [run]'
    exit
  fi
elif [ "$#" -gt 1 ]; then
  echo 'usage: ./build.sh [run]'
  exit
fi

if [ ! -d "${BUILD_PATH}" ]; then
  mkdir ${BUILD_PATH}
  echo 'build directory created'
fi
chmod 775 ${BUILD_PATH}

cd ${BUILD_PATH}
cmake ${SDE}/p4studio/ \
  -DCMAKE_INSTALL_PREFIX=${SDE}/install \
  -DCMAKE_MODULE_PATH=${SDE}/cmake \
  -DP4_NAME=${P4_NAME} \
  -DP4_PATH=${SRC_PATH}/${P4_NAME}.p4

make ${P4_NAME} && sudo -u sde make install

if [ "$is_run" -eq 1 ]; then
  echo "run switchd"
  $SDE/run_switchd.sh -p ${P4_NAME}
fi
