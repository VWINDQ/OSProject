#!/usr/bin/env bash
# STATUS throughput and latency at 1, 3 and 8 workers.
# Usage: bash scripts/load_test.sh [clients=16] [seconds=5]
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

CLIENTS="${1:-16}"
SECONDS_PER_RUN="${2:-5}"
QUEUE_FILE=/dev/mqueue/cinema_request
server_pid=""
failed=0

stop_server() {
    if [[ -n "${server_pid}" ]]; then
        kill -TERM "${server_pid}" 2>/dev/null || true
        wait "${server_pid}" 2>/dev/null || true
        server_pid=""
    fi
}
trap stop_server EXIT

# field <name> <result line>: the value of name=value in the RESULT line
field() {
    sed -n "s/.* $1=\([^ ]*\).*/\1/p" <<<"$2"
}

printf 'clients=%s  seconds per run=%s  command=STATUS  server log discarded\n\n' "${CLIENTS}" "${SECONDS_PER_RUN}"
printf '%-8s %10s %9s %9s %9s %9s %9s %7s\n' workers req/s mean_ms p50_ms p95_ms p99_ms max_ms errors

for workers in 1 3 8; do
    rm -f "${QUEUE_FILE}"
    ./server sync "${workers}" 0 0 > /dev/null 2>&1 &
    server_pid=$!
    for _ in $(seq 1 100); do
        [[ -e "${QUEUE_FILE}" ]] && break
        sleep 0.05
    done

    status=0
    result="$(./load_test "${CLIENTS}" "${SECONDS_PER_RUN}")" || status=$?
    if [[ "${status}" -ne 0 ]]; then
        failed=1
    fi
    printf '%-8s %10s %9s %9s %9s %9s %9s %7s\n' "${workers}" \
        "$(field rps "${result}")" "$(field mean_ms "${result}")" \
        "$(field p50_ms "${result}")" "$(field p95_ms "${result}")" \
        "$(field p99_ms "${result}")" "$(field max_ms "${result}")" \
        "$(field errors "${result}")"
    stop_server
done

if [[ "${failed}" -ne 0 ]]; then
    echo "LOAD TEST FAILED: some requests got no valid reply" >&2
    exit 1
fi
