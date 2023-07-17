#!/bin/bash

add_echo () {
  echo "echo \"$@\"" >> ${BATCH_FILE}
}

add_cmd () {
  echo "$@" >> ${BATCH_FILE}
}

add_cmd_verbose () {
  echo "echo \"$@\"" >> ${BATCH_FILE}
  echo "$@" >> ${BATCH_FILE}
}

show_total_dram_usages() {
  add_cmd "pdsh free -g | grep Mem | awk '{used+=\$4; cache+=\$7} END{print \"Total DRAM Usages (GB) --- used, cache: \" used \" \" cache}'"
}

main() {
  # Run the benchmark
  for d in deep bigann; do
    for m in 4 8 16 32 64; do
      for efc in 25 50 100; do
        JOB_NAME="hnswlib-bench-parallel-${d}-m${m}-efc${efc}"
        BATCH_FILE="run-${JOB_NAME}.sbatch"

        echo "#!/bin/bash" >  ${BATCH_FILE}

        add_cmd "#SBATCH" -N 1
        add_cmd "#SBATCH" --ntasks-per-node 1
        add_cmd "#SBATCH" -t 14:00:00
        add_cmd "#SBATCH" -A seq

        local out_file="out-${JOB_NAME}.log"
        add_cmd "#SBATCH" -o  ${out_file}
        local err_file="err-${JOB_NAME}.log"
        add_cmd "#SBATCH" -e  ${err_file}

        if [[ ${d} == "deep" ]]; then
          dims=96
        elif [[ ${d} == "bigann" ]]; then
          dims=128
        fi

        local param="-p big-ann-dataset/txt/${d}-base/ -d ${dims} -n $((2**30)) -m ${m} -c ${efc} -k 10 -e 25:50:100:200:400:600:800:1000:1200:1400:1600:1800:2000:2200:2400:2600:2800 -q big-ann-dataset/txt/${d}-query.txt -g big-ann-dataset/txt/${d}-gt.txt"
        add_cmd date
        add_cmd_verbose ./run_hnswlib_bench ${param}
        add_echo ""
        add_cmd date
        show_total_dram_usages
      done
    done
  done
}

main "$@"