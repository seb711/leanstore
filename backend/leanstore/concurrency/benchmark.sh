#!/bin/bash

# Define common parameters
SSD_PATH="testfile"
IOENGINE="libaio"
PARTITION_BITS=3
PP_THREADS=1
XMERGE=1
CONT_SPLIT=1
OPTIMISTIC_PARENT_POINTER=1
RUN_SECONDS=30

# TPCC specific parameters
TPCC_WAREHOUSE_COUNT=10
TPCC_DRAM_GIB=6
TPCC_ASYNC_BATCH_SIZE=1
TPCC_WORKER_TASKS=16

# YCSB specific parameters
YCSB_TARGET_GIB=4
YCSB_DRAM_GIB=6
YCSB_READ_RATIO=100
YCSB_WORKER_TASKS=16

# OSV specific parameters
OSV_CORES=5
OSV_RAM="8G"

build_benchmark() {
  ./scripts/build -j4 fs=zfs image=leanstore app_local_exec_tls_size=130000 mode=release
}

setup_benchmark() {
  touch $SSD_PATH
  ./scripts/run.py -e "/touch $SSD_PATH" -r
}

# Benchmark execution function
run_benchmarks() {
    local WORKER_THREADS=$1
    local OUTPUT_SUFFIX=$2

    # Host TPCC single-threaded/multi-threaded
    ./apps/leanstore/build/frontend/tpcc \
        --ssd_path=$SSD_PATH \
        --ioengine=$IOENGINE \
        --nopp=true \
        --partition_bits=$PARTITION_BITS \
        --tpcc_warehouse_count=$TPCC_WAREHOUSE_COUNT \
        --run_for_seconds=$RUN_SECONDS \
        --dram_gib=$TPCC_DRAM_GIB \
        --worker_tasks=$TPCC_WORKER_TASKS \
        --async_batch_size=$TPCC_ASYNC_BATCH_SIZE \
        --optimistic_parent_pointer=$OPTIMISTIC_PARENT_POINTER \
        --xmerge=$XMERGE \
        --contention_split=$CONT_SPLIT \
        --worker_threads=$WORKER_THREADS \
        --pp_threads=$PP_THREADS \
        > "./benchmark_results/${OUTPUT_SUFFIX}_tpcc_host.csv"

    # Host YCSB single-threaded/multi-threaded
    ./apps/leanstore/build/frontend/ycsb \
        --ssd_path=$SSD_PATH \
        --ioengine=$IOENGINE \
        --partition_bits=$PARTITION_BITS \
        --target_gib=$YCSB_TARGET_GIB \
        --ycsb_read_ratio=$YCSB_READ_RATIO \
        --run_for_seconds=$RUN_SECONDS \
        --dram_gib=$YCSB_DRAM_GIB \
        --worker_tasks=$YCSB_WORKER_TASKS \
        --optimistic_parent_pointer=$OPTIMISTIC_PARENT_POINTER \
        --xmerge=$XMERGE \
        --contention_split=$CONT_SPLIT \
        --nopp=true \
        --worker_threads=$WORKER_THREADS \
        --pp_threads=$PP_THREADS \
        > "./benchmark_results/${OUTPUT_SUFFIX}_ycsb_host.csv"

    # OSV TPCC single-threaded/multi-threaded
    ./scripts/run.py -e "/tpcc --ssd_path=$SSD_PATH --ioengine=$IOENGINE --nopp=true --partition_bits=$PARTITION_BITS --tpcc_warehouse_count=$TPCC_WAREHOUSE_COUNT --run_for_seconds=$RUN_SECONDS --dram_gib=$TPCC_DRAM_GIB --worker_tasks=$TPCC_WORKER_TASKS --async_batch_size=$TPCC_ASYNC_BATCH_SIZE --optimistic_parent_pointer=$OPTIMISTIC_PARENT_POINTER --xmerge=$XMERGE --contention_split=$CONT_SPLIT --worker_threads=$WORKER_THREADS --pp_threads=$PP_THREADS" -c$OSV_CORES -m$OSV_RAM -r > "./benchmark_results/${OUTPUT_SUFFIX}_tpcc_osv.csv"

    # OSV YCSB single-threaded/multi-threaded
    ./scripts/run.py -e "/ycsb --ssd_path=$SSD_PATH --ioengine=$IOENGINE --partition_bits=$PARTITION_BITS --target_gib=$YCSB_TARGET_GIB --ycsb_read_ratio=$YCSB_READ_RATIO --run_for_seconds=$RUN_SECONDS --dram_gib=$YCSB_DRAM_GIB --worker_tasks=$YCSB_WORKER_TASKS --optimistic_parent_pointer=$OPTIMISTIC_PARENT_POINTER --xmerge=$XMERGE --contention_split=$CONT_SPLIT --nopp=true --worker_threads=$WORKER_THREADS --pp_threads=$PP_THREADS" -c$OSV_CORES -m$OSV_RAM -r > "./benchmark_results/${OUTPUT_SUFFIX}_ycsb_osv.csv"
}

# Execute benchmarks
mkdir -p benchmark_results
build_benchmark
setup_benchmark
run_benchmarks 1 "single"
run_benchmarks 3 "multi"
