#!/bin/bash

P4_NAME=incast_detect
BFRT_NAME=tna_exact_match
SRC_PATH=/root/bf-sde-9.7.0/home/cxm/bfrt_examples
BUILD_PATH=${SRC_PATH}/build
INSTALL_PATH=${SDE_INSTALL}
CONF_FILE_PATH=${SDE_INSTALL}/share/p4/targets/tofino/${P4_NAME}.conf
is_run=0
shell_flag=""

if [ ! -d "${BUILD_PATH}" ]; then
  mkdir ${BUILD_PATH}
  echo 'build directory created'
fi

if [ "$#" -le 2 ]; then
  case "$1" in
  run) is_run=1 ;;
  "") is_run=0 ;;
  *)
    echo 'usage: ./build.sh [run] [bfshell|ucli|noshell]'
    exit
    ;;
  esac

  case "$2" in
  bfshell) shell_flag="bfshell" ;;
  ucli) shell_flag="ucli" ;;
  "") shell_flag="noshell" ;;
  *)
    echo 'usage: ./build.sh [run] [bfshell|ucli|noshell]'
    exit
    ;;
  esac
elif [ "$#" -gt 2 ]; then
  echo 'usage: ./build.sh [run] [bfshell|ucli|noshell]'
  exit
fi

cd ${BUILD_PATH}

sudo ln -s /root/bf-sde-9.7.0/install/lib/libpltfm_mgr_thrift.so /usr/lib/libpltfm_mgr_thrift.so

cmake \
  -DCMAKE_INSTALL_PREFIX=${INSTALL_PATH} \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=1 \
  ..

make && make install

if [ ! -L "${SRC_PATH}/compile_commands.json" ]; then
  ln -s ${BUILD_PATH}/compile_commands.json ${SRC_PATH}/compile_commands.json
  echo 'symbolic compile_commands.json created'
fi

if [ "$is_run" -eq 1 ]; then
  echo "run switchd"

  # work around for bfshell not find dynamic link problem. no idea why it cannot find link path specified in cmake
  # sudo ln -s /home/sde/bf-sde/bf-sde-9.13.3/install/lib/bfshell_plugin_pipemgr.so /usr/lib/bfshell_plugin_pipemgr.so
  # sudo ln -s /home/sde/bf-sde/bf-sde-9.13.3/install/lib/bfshell_plugin_bf_rt.so /usr/lib/bfshell_plugin_bf_rt.so
  # sudo ln -s /home/sde/bf-sde/bf-sde-9.13.3/install/lib/bfshell_plugin_debug.so /usr/lib/bfshell_plugin_debug.so
  # sudo ln -s /home/sde/bf-sde/bf-sde-9.13.3/install/lib/bfshell_plugin_clish.so /usr/lib/bfshell_plugin_clish.so
  # sudo ln -s /root/bf-sde-9.7.0/install/lib/libpltfm_mgr.so /usr/lib/libpltfm_mgr.so

  case "$shell_flag" in
  "bfshell")
    sudo ${INSTALL_PATH}/bin/${BFRT_NAME} --install-dir=$SDE_INSTALL --conf-file=${CONF_FILE_PATH} --bfshell
    ;;
  ucli)
    sudo ${INSTALL_PATH}/bin/${BFRT_NAME} --install-dir=$SDE_INSTALL --conf-file=${CONF_FILE_PATH} --ucli
    ;;
  noshell)
    sudo ${INSTALL_PATH}/bin/${BFRT_NAME} --install-dir=$SDE_INSTALL --conf-file=${CONF_FILE_PATH}
    ;;
  *) ;;
  esac

  # sudo rm /usr/lib/libpltfm_mgr.so
  # sudo rm /usr/lib/bfshell_plugin_pipemgr.so
  # sudo rm /usr/lib/bfshell_plugin_bf_rt.so
  # sudo rm /usr/lib/bfshell_plugin_debug.so
  # sudo rm /usr/lib/bfshell_plugin_clish.so
  sudo rm /usr/lib/libpltfm_mgr_thrift.so
fi
