#!/bin/bash

m_list=(4 8 16 32 64)
efc_list=(25 50 100)

echo -n "dataset M ef_const const_time"
ef_list=(25 50 100 200 400 600 800 1000 1200 1400 1600 1800 2000 2200 2400 2600 2800)
for ef in "${ef_list[@]}"; do
  echo -n " ef=${ef}"
done
echo ""

for d in deep bigann; do
  for m in "${m_list[@]}"; do
    for efc in "${efc_list[@]}"; do
      echo -n $d " "  $m " " $efc

      JOB_NAME="hnswlib-bench-parallel-${d}-m${m}-efc${efc}"
      out_file="out-${JOB_NAME}.log"

      const_t=$(grep "k-NNG construction time" ${out_file} | awk '{print $5}')
      echo -n " " $const_t

      readarray -t q_times < <(grep "k-NNG search time" ${out_file} | awk '{print $5}')
      for t in "${q_times[@]}"; do
        echo -n " " $t
      done
      readarray -t q_recalls < <(grep "Exact" ${out_file} | awk '{print $8}')
      for r in "${q_recalls[@]}"; do
        echo -n " " $r
      done
      echo ""
    done
  done
done

# for d in deep bigann; do
#   for m in "${m_list[@]}"; do
#     for efc in "${efc_list[@]}"; do

#       OLD_JOB_NAME="hnswlib-bench-parallel-${d}-m${m}-cfc${efc}"
#       OLD_BATCH_FILE="run-${OLD_JOB_NAME}.sbatch"
#       old_out_file="out-${OLD_JOB_NAME}.log"
#       old_err_file="err-${OLD_JOB_NAME}.log"

#       NEW_JOB_NAME="hnswlib-bench-parallel-${d}-m${m}-efc${efc}"
#       NEW_BATCH_FILE="run-${NEW_JOB_NAME}.sbatch"
#       new_out_file="out-${NEW_JOB_NAME}.log"
#       new_err_file="err-${NEW_JOB_NAME}.log"

#       mv ${OLD_BATCH_FILE} ${NEW_BATCH_FILE}
#       mv ${old_out_file} ${new_out_file}
#       mv ${old_err_file} ${new_err_file}
#     done
#   done
# done