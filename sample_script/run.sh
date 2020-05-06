#!/bin/bash

# Your gem5 path
GEM5_PATH=/home/SDO

# Your executable path
EXE_PATH=/home/a.out

# Your gem5 output directory
OUT_DIR=$GEM5_PATH/output

# Your gem5 configuration file
CONFIG_FILE=$GEM5_PATH/configs/example/se.py


NUM_CORES=1
MAX_INSTRS=100000000

# Common configuration parameters
gem5_common_config="--num-cpus=$NUM_CORES --mem-size=4GB --num-l2caches=$NUM_CORES \
--cpu-type=DerivO3CPU \
--num-dirs=$NUM_CORES --ruby --maxinsts=$NUM_INSTRS \
--network=simple --topology=Mesh_XY --mesh-rows=1 --mem_model=TSO --MSHR_size=16"

##
##  Run Unsafe baseline
##
$GEM5_PATH/build/X86_MESI_Three_Level/gem5.opt --outdir=$OUT_DIR \
    $CONFIG_FILE \
    $gem5_common_config \
    --scheme=UnsafeBaseline \
    -c $EXE_PATH


##
##  Run STT
##
$GEM5_PATH/build/X86_MESI_Three_Level/gem5.opt --outdir=$OUT_DIR \
    $CONFIG_FILE \
    $gem5_common_config \
    --threat_model=Spectre \
    --STT=1 --impChannel=1 \
    -c $EXE_PATH


##
##  Run SDO
##
$GEM5_PATH/build/X86_MESI_Three_Level/gem5.opt --outdir=$OUT_DIR \
    $CONFIG_FILE \
    $gem5_common_config \
    --threat_model=Spectre \
    --STT=1 --impChannel=1 \
    --enable_SDO --pred_type=tournament_2way \
    --subpred1_type=greedy --subpred2_type=loop \
    --pred_option=0 --TLB_defense=SDO \
    -c $EXE_PATH
