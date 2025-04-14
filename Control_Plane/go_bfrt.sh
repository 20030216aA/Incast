
#!/bin/bash

set -e

# Mandatory arguments
if [ "$#" -lt 1 ]; then
    echo "Usage: $0 <myprog> [cli]" >&2
    exit 1
fi

myprog=$1

# echo "building p4 program ${myprog}"
# /root/bf-sde-9.7.0/home/build_p4.sh cxm ${myprog}  # build p4 program

echo -e "\033[31mWARNING: using tna_exact_match.cpp for building the program, <myprog> should be your P4 program name and it should be compiled already\033[0m"

if [ "$#" -eq 2 ]; then
    cli=$2  # default is build, if run, then run switchd
    if [ "$cli" = "bfshell" ]; then
        cp /root/bf-sde-9.7.0/home/cxm/bfrt_examples/tna_exact_match.cpp /root/bf-sde-9.7.0/pkgsrc/bf-drivers/bf_switchd/bfrt_examples/
        echo "source file copied into pkgsrc"
        cd $SDE/build/pkgsrc/bf-drivers/bf_switchd/bfrt_examples
        make tna_exact_match_example/fast
        ./tna_exact_match_example --install-dir=$SDE_INSTALL --conf-file=/root/bf-sde-9.7.0/install/share/p4/targets/tofino/${myprog}.conf --bfshell
    else
        echo "invalid argument"
    fi
fi

if [ "$#" -eq 1 ]; then
    cp /root/bf-sde-9.7.0/home/cxm/bfrt_examples/tna_exact_match.cpp /root/bf-sde-9.7.0/pkgsrc/bf-drivers/bf_switchd/bfrt_examples/
    echo "source file copied into pkgsrc"
    cd $SDE/build/pkgsrc/bf-drivers/bf_switchd/bfrt_examples
    make tna_exact_match_example/fast
    ./tna_exact_match_example --install-dir=$SDE_INSTALL --conf-file=/root/bf-sde-9.7.0/install/share/p4/targets/tofino/${myprog}.conf
fi




