#!/bin/bash
set -e

DIR=$(cd $(dirname "${BASH_SOURCE[0]}") && pwd)
cd $DIR

print_help() {
    echo "Options:"
    echo "  setup <target>:         Prepare target context for benchmarking"
    echo "  run <target> <bench>:   Run bench on target"
    echo "  clean <target>:       Destroys target context"
    echo ""
}

# examples:
#    sudo ./benchmark.sh setup f2fs /mnt/f2fs/ nvme6n1 nvme1n1
#    sudo ./benchmark.sh clean f2fs /mnt/f2fs/ nvme6n1
#    sudo ./benchmark.sh setup znslsm 0000:00:04.0
#    sudo ./benchmark.sh clean znslsm 0000:00:04.0
#    sudo LD_LIBRARY_PATH="LD_LIBRARY_PATH:/home/user/spdk/dpdk/build/lib" ./benchmark.sh setup zenfs nvme6n1
#    sudo LD_LIBRARY_PATH="LD_LIBRARY_PATH:/home/user/spdk/dpdk/build/lib" ./benchmark.sh clean zenfs nvme6n1

if [[ $# -le 2 ]] ; then
    echo ""
    echo "Not enough arguments given, please provide the function you want to use..."
    echo ""
    print_help
    exit 1
fi

setup_bench() {
case $1 in
    "f2fs")
        shift
        ./utils/f2fs_utils.sh create $*
        exit $?
    ;;
    "zenfs")
        shift
        ./utils/zenfs_utils.sh create $*
        exit $?
    ;;
    "znslsm")
        shift
        ./utils/znslsm_utils.sh create $*
        exit $?
    ;;
    *)
        echo "This target is not known..."
        exit 1
    ;;
esac
}

function print_duration() {
  SECS=$1
  HRS=$(($SECS / 3600))
  SECS=$(($SECS % 3600))
  MINS=$(($SECS / 60))
  SECS=$(($SECS % 60))
  echo "$HRS"h "$MINS"m "$SECS"s
}

default_perf() {
    # db configs
    NUM=8000     # Please set to > 80% of device
    KSIZE=16        # default
    VSIZE=1000      # Taken from ZenFS benchmarks
    ZONE_CAP=512    # Alter for device
    TARGET_FILE_SIZE_BASE=$(($ZONE_CAP * 2 * 95 / 100)) 
    # ^ Taken from ZenFS?
    WB_SIZE=$(( 2 * 1024 * 1024 * 1024)) # Again ZenFS, 2GB???
    threads=3

    echo "$(tput setaf 3)Running quick performance fillseq $(tput sgr 0)"
    TEST_OUT="./output/default_${BENCHMARKS}_${TARGET}"
    SECONDS=0
    START_SECONDS=$SECONDS

    echo "Starting benchmark $BENCHMARKS at $TARGET" > $TEST_OUT
    ../db_bench $EXTRA_DB_BENCH_ARGS                \
        --num=$NUM                                  \
        --compression_type=None                     \
        --value_size=$VSIZE --key_size=$KSIZE       \
        --use_direct_io_for_flush_and_compaction    \
        --use_direct_reads                          \
        --max_bytes_for_level_multiplier=4          \
        --max_background_jobs=8                     \
        --target_file_size_base=$ZONE_CAP           \
        --write_buffer_size=$WB_SIZE                \
        --histogram                                 \
        --threads=$threads                          \
        --benchmarks=$BENCHMARKS                    \
        >> $TEST_OUT
    echo ""
    echo "Test duration $(print_duration $(($SECONDS - $START_SECONDS)))" | tee -a $TEST_OUT
}

run_bench_quick_performance() {
    ZONE_CAP=512    # Alter for device
    TARGET_FILE_SIZE_BASE=$(($ZONE_CAP * 2 * 95 / 100)) 
    # ^ Taken from ZenFS?
    WB_SIZE=$(( 2 * 1024 * 1024 * 1024)) # Again ZenFS, 2GB???

    WORKLOAD_SZ=10000000000
    WORKLOAD_SZ=100000000 # < Please comment this line on production

    echo "$(tput setaf 3)Running quick performance fillseq $(tput sgr 0)"
    TEST_OUT="./output/quick_fillseq_${TARGET}"
    echo "" > $TEST_OUT
    SECONDS=0
    BENCHMARKS=fillseq
    for VALUE_SIZE in 100 200 400 1000 2000 8000; do
        START_SECONDS=$SECONDS
        NUM=$(( $WORKLOAD_SZ / $VALUE_SIZE ))
        ../db_bench $EXTRA_DB_BENCH_ARGS                \
            --num=$NUM                                  \
            --compression_type=none                     \
            --value_size=$VALUE_SIZE --key_size=16      \
            --use_direct_io_for_flush_and_compaction    \
            --use_direct_reads                          \
            --max_bytes_for_level_multiplier=4          \
            --max_background_jobs=8                     \
            --target_file_size_base=$ZONE_CAP           \
            --write_buffer_size=$WB_SIZE                \
            --histogram                                 \
            --benchmarks=$BENCHMARKS                    \
            >> $TEST_OUT
        echo ""
        echo "Test duration for val size $VALUE_SIZE $(print_duration $(($SECONDS - $START_SECONDS)))" | tee -a $TEST_OUT
    done

    echo "$(tput setaf 3)Running quick performance fillrandom $(tput sgr 0)"
    TEST_OUT="./output/quick_fillrandom_${TARGET}"
    echo "" > $TEST_OUT
    SECONDS=0
    BENCHMARKS=fillrandom
    for VALUE_SIZE in 100 200 400 1000 2000 8000; do
        START_SECONDS=$SECONDS
        NUM=$(( $WORKLOAD_SZ / $VALUE_SIZE ))
        ../db_bench $EXTRA_DB_BENCH_ARGS                \
            --num=$NUM                                  \
            --compression_type=none                     \
            --value_size=$VALUE_SIZE --key_size=16      \
            --use_direct_io_for_flush_and_compaction    \
            --use_direct_reads                          \
            --max_bytes_for_level_multiplier=4          \
            --max_background_jobs=8                     \
            --target_file_size_base=$ZONE_CAP           \
            --write_buffer_size=$WB_SIZE                \
            --histogram                                 \
            --benchmarks=$BENCHMARKS                    \
            >> $TEST_OUT
        echo ""
        echo "Test duration for val size $VALUE_SIZE $(print_duration $(($SECONDS - $START_SECONDS)))" | tee -a $TEST_OUT
    done
}

run_bench() {
    if [[ $# -le 2 ]] ; then
        echo ""
        echo "Not enough arguments given, please provide a target and benchmark..."
        echo "F2FS also requires the mounted path, Zenfs requires the device name"
        echo "and lsmkv requires the trid."
        echo ""
        exit 1
    fi
    if [[ ! -f ../db_bench ]] ; then
        echo ""
        echo "db_bench not found, please recompile db_bench in the RocksDB directory"
        echo ""
        exit 1
    fi

    # Set args
    BENCHMARKS=$1
    TARGET=$2
    OPT=$3

    export BENCHMARKS
    export TARGET
    export OPT

    # Setup db bench args for specific environment
    EXTRA_DB_BENCH_ARGS=""
    case $TARGET in
    "f2fs")
        F2FS_ARGS="--db=$OPT/db0 --wal_dir=$OPT/wal0"
        EXTRA_DB_BENCH_ARGS="$EXTRA_DB_BENCH_ARGS $F2FS_ARGS"
    ;;
    "zenfs")
        ZENFS_ARGS="-fs_uri=zenfs://dev:$OPT"
        EXTRA_DB_BENCH_ARGS="$EXTRA_DB_BENCH_ARGS $ZENFS_ARGS"
    ;;
    "znslsm")
        ZNSLSM_ARGS="--use_zns=true --db=$OPT"
        EXTRA_DB_BENCH_ARGS="$EXTRA_DB_BENCH_ARGS $ZNSLSM_ARGS"
    ;;
    *)
        echo "This target is not known..."
        exit 1
    ;;
    esac

    export EXTRA_DB_BENCH_ARGS

    case $BENCHMARKS in
    "quick")
        run_bench_quick_performance
    ;;
    *)
        default_perf
    ;;
    esac


    return $?
}

clean_bench() {
case $1 in
    "f2fs")
        shift
        ./utils/f2fs_utils.sh destroy $*
        exit $?
    ;;
    "zenfs")
        shift
        ./utils/zenfs_utils.sh destroy $*
        exit $?
    ;;
    "znslsm")
        shift
        ./utils/znslsm_utils.sh destroy $*
        exit $?
    ;;
    *)
        echo "This target is not known..."
        exit 1
    ;;
esac
}

ACTION=$1
case $ACTION in 
    "setup")
        shift
        setup_bench $*
        exit $?
    ;;
    "run")
        shift
        run_bench $*
        exit $?
    ;;
    "clean")
        shift
        clean_bench $*
        exit $?
    ;;
    *)
        echo "Unknown command..."
        echo ""
        print_help
        exit 1
    ;;
esac
