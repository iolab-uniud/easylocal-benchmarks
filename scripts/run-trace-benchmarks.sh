#!/usr/bin/env bash
# Build and run the tracing benchmarks of infrastructure/, and record the
# machine:
#
#   run-trace-benchmarks.sh RESULTS_DIR [TRIALS]
#
# writes RESULTS_DIR/trace.csv (trial,mode,ns_per_evaluation,checksum),
# RESULTS_DIR/trace-cost-encoding.csv (trial,cost_model,ns_per_event,
# bytes_per_event,event_count) and RESULTS_DIR/trace-metadata.csv.
# EASYLOCAL_SOURCE_DIR is the EasyLocal checkout to measure (default:
# easylocal/ in this repository), BENCHMARK_BUILD_DIR the build directory.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
easylocal_dir="${EASYLOCAL_SOURCE_DIR:-${repo_root}/easylocal}"
results_dir="${1:?usage: run-trace-benchmarks.sh RESULTS_DIR [TRIALS]}"
trials="${2:-5}"
build_dir="${BENCHMARK_BUILD_DIR:-${repo_root}/build/trace-benchmark}"

if [[ ! "${trials}" =~ ^[1-9][0-9]*$ ]]; then
    echo "error: trials must be a positive integer" >&2
    exit 2
fi
mkdir -p "${results_dir}"

cmake_args=()
if [[ -n "${SDKROOT:-}" ]]; then
    cmake_args+=("-DCMAKE_OSX_SYSROOT=${SDKROOT}")
fi
{
    cmake -S "${repo_root}/infrastructure" -B "${build_dir}" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DEASYLOCAL_SOURCE_DIR="${easylocal_dir}" \
        "${cmake_args[@]}"
    cmake --build "${build_dir}" --target \
        easylocal_trace_benchmark easylocal_trace_cost_encoding_benchmark
} > "${results_dir}/trace-build.log" 2>&1

python3 "${repo_root}/scripts/machine-metadata.py" \
    --output "${results_dir}/trace-metadata.csv" \
    --repo-root "${easylocal_dir}" \
    --trials "${trials}"

echo 'trial,mode,ns_per_evaluation,checksum' > "${results_dir}/trace.csv"
echo 'trial,cost_model,ns_per_event,bytes_per_event,event_count' \
    > "${results_dir}/trace-cost-encoding.csv"
for trial in $(seq 1 "${trials}"); do
    "${build_dir}/easylocal_trace_benchmark" 2>> "${results_dir}/trace.log" \
        | tail -n +2 | sed "s/^/${trial},/" >> "${results_dir}/trace.csv"
    "${build_dir}/easylocal_trace_cost_encoding_benchmark" \
        2>> "${results_dir}/trace.log" \
        | tail -n +2 | sed "s/^/${trial},/" >> "${results_dir}/trace-cost-encoding.csv"
done
