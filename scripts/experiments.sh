#!/usr/bin/env bash
# Race Condition experiments and demo (run inside the container):
#   bash scripts/experiments.sh [mixed|1|2|3|all]
# Environment: ROUNDS (default 5), RESULTS_DIR (default results)
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

ROUNDS="${ROUNDS:-5}"
RESULTS_DIR="${RESULTS_DIR:-results}"
CLIENT_COUNT=5
SEAT=10
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

# start_server <log_file> <server arguments...>
start_server() {
    local log_file="$1"
    local attempt

    shift
    rm -f "${QUEUE_FILE}"
    ./server "$@" > "${log_file}" 2>&1 &
    server_pid=$!
    for attempt in $(seq 1 100); do
        if grep -Fq "started:" "${log_file}"; then
            return 0
        fi
        sleep 0.05
    done
    echo "server did not start; see ${log_file}" >&2
    return 1
}

# run_client <client_id> <command...>: send one command, then QUIT
run_client() {
    local id="$1"

    shift
    printf '%s\nQUIT\n' "$*" | ./client "${id}" 2>&1
}

# check <label> <file> <fixed text that must be in the file>
check() {
    if grep -Fq -- "$3" "$2"; then
        echo "  [PASS] $1" | tee -a "${summary}"
    else
        echo "  [FAIL] $1 (expected '$3' in $2)" | tee -a "${summary}"
        failed=1
    fi
}

# One round: 5 clients send RESERVE 10 at the same time.
# Sets: round_successes, round_races, round_final_owner
reserve_round() {
    local dir="$1" mode="$2" workers="$3" round="$4"
    local log="${dir}/server-round${round}.log"
    local table="${dir}/clients-round${round}.txt"
    local tmp id verdict pids=()

    tmp="$(mktemp -d)"
    start_server "${log}" "${mode}" "${workers}"
    for id in $(seq 1 "${CLIENT_COUNT}"); do
        run_client "${id}" "RESERVE ${SEAT}" > "${tmp}/client-${id}.txt" &
        pids+=("$!")
    done
    wait "${pids[@]}"
    run_client 99 "STATUS ${SEAT}" > "${tmp}/final.txt"
    stop_server

    round_successes=0
    : > "${table}"
    for id in $(seq 1 "${CLIENT_COUNT}"); do
        if grep -Fq "SUCCESS: Seat ${SEAT} reserved successfully." "${tmp}/client-${id}.txt"; then
            verdict=SUCCESS
            round_successes=$((round_successes + 1))
        else
            verdict=FAILED
        fi
        echo "Client ${id} : ${verdict}" >> "${table}"
    done
    round_races="$(grep -c 'RACE DETECTED' "${log}" || true)"
    round_final_owner="$(sed -n 's/.*reserved by Client \([0-9]*\)\..*/\1/p' "${tmp}/final.txt")"
    round_final_owner="${round_final_owner:-none}"
    rm -rf "${tmp}"
}

# run_reserve_experiment <dir> <title> <mode> <workers> <one|race>
run_reserve_experiment() {
    local name="$1" title="$2" mode="$3" workers="$4" expectation="$5"
    local dir="${RESULTS_DIR}/${name}"
    local round bad_rounds=0 race_rounds=0 total_successes=0

    rm -rf "${dir}"
    mkdir -p "${dir}"
    summary="${dir}/summary.txt"
    {
        echo "${title}"
        echo "server: ./server ${mode} ${workers}   clients: ${CLIENT_COUNT} x RESERVE ${SEAT}   rounds: ${ROUNDS}"
        echo
    } | tee "${summary}"

    for round in $(seq 1 "${ROUNDS}"); do
        reserve_round "${dir}" "${mode}" "${workers}" "${round}"
        echo "round ${round}: SUCCESS=${round_successes} RACE_DETECTED=${round_races} final_owner=${round_final_owner}" | tee -a "${summary}"
        if [[ "${round}" -eq 1 ]]; then
            sed 's/^/    /' "${dir}/clients-round1.txt" | tee -a "${summary}"
        fi
        total_successes=$((total_successes + round_successes))
        if [[ "${round_successes}" -ge 2 && "${round_races}" -ge 1 ]]; then
            race_rounds=$((race_rounds + 1))
        fi
        if [[ "${round_successes}" -ne 1 || "${round_races}" -ne 0 ]]; then
            bad_rounds=$((bad_rounds + 1))
        fi
    done

    {
        echo
        echo "rounds with more than one SUCCESS and a RACE DETECTED line: ${race_rounds} of ${ROUNDS}"
        echo "rounds with exactly one SUCCESS and no race: $((ROUNDS - bad_rounds)) of ${ROUNDS}"
    } | tee -a "${summary}"

    if [[ "${expectation}" == "one" ]]; then
        if [[ "${bad_rounds}" -eq 0 ]]; then
            echo "RESULT: PASS (exactly one client reserved seat ${SEAT} in every round)" | tee -a "${summary}"
        else
            echo "RESULT: FAIL (${bad_rounds} round(s) did not end with exactly one SUCCESS)" | tee -a "${summary}"
            failed=1
        fi
    else
        if [[ "${race_rounds}" -ge 1 ]]; then
            echo "RESULT: PASS (race condition observed in ${race_rounds} of ${ROUNDS} rounds)" | tee -a "${summary}"
        else
            echo "RESULT: FAIL (no race condition observed in ${ROUNDS} rounds)" | tee -a "${summary}"
            failed=1
        fi
    fi
    echo
}

run_mixed_demo() {
    local dir="${RESULTS_DIR}/mixed-demo"
    local commands=("RESERVE 3" "RESERVE 7" "RESERVE 12" "STATUS 10" "LIST")
    local tmp id started finished pids=()

    rm -rf "${dir}"
    mkdir -p "${dir}"
    summary="${dir}/summary.txt"
    {
        echo "Demo 1: five clients send different commands at the same time"
        echo "server: ./server sync 3"
        echo
    } | tee "${summary}"

    tmp="$(mktemp -d)"
    start_server "${dir}/server.log" sync 3
    started="$(date +%s%3N)"
    for id in $(seq 1 "${CLIENT_COUNT}"); do
        run_client "${id}" "${commands[$((id - 1))]}" > "${tmp}/client-${id}.txt" &
        pids+=("$!")
    done
    wait "${pids[@]}"
    finished="$(date +%s%3N)"
    run_client 99 "LIST" > "${tmp}/final-list.txt"
    stop_server

    : > "${dir}/clients.txt"
    for id in $(seq 1 "${CLIENT_COUNT}"); do
        {
            echo "--- Client ${id}: ${commands[$((id - 1))]}"
            cat "${tmp}/client-${id}.txt"
        } >> "${dir}/clients.txt"
    done
    {
        echo "--- final LIST (Client 99)"
        cat "${tmp}/final-list.txt"
    } >> "${dir}/clients.txt"

    echo "all five clients finished in $((finished - started)) ms" | tee -a "${summary}"
    check "Client 1 reserved seat 3" "${tmp}/client-1.txt" "SUCCESS: Seat 3 reserved successfully."
    check "Client 2 reserved seat 7" "${tmp}/client-2.txt" "SUCCESS: Seat 7 reserved successfully."
    check "Client 3 reserved seat 12" "${tmp}/client-3.txt" "SUCCESS: Seat 12 reserved successfully."
    check "Client 4 got STATUS 10" "${tmp}/client-4.txt" "SUCCESS: Seat 10 is available."
    check "Client 5 got LIST" "${tmp}/client-5.txt" "SUCCESS: Seats ("
    check "final LIST shows seat 3 owned by Client 1" "${tmp}/final-list.txt" " 3:C1"
    check "final LIST shows seat 7 owned by Client 2" "${tmp}/final-list.txt" " 7:C2"
    check "final LIST shows seat 12 owned by Client 3" "${tmp}/final-list.txt" " 12:C3"
    if grep -Fq "RACE DETECTED" "${dir}/server.log"; then
        echo "  [FAIL] server log contains RACE DETECTED" | tee -a "${summary}"
        failed=1
    fi
    rm -rf "${tmp}"
    echo
}

run_experiment_1() {
    run_reserve_experiment exp1-sequential \
        "Experiment 1: Sequential baseline (one worker)" sync 1 one
}

run_experiment_2() {
    run_reserve_experiment exp2-race \
        "Experiment 2: Concurrent without synchronization (race condition)" nosync 3 race
}

run_experiment_3() {
    run_reserve_experiment exp3-mutex \
        "Experiment 3: Concurrent with mutex (same workers, same random delay)" sync 3 one
}

case "${1:-all}" in
    mixed) run_mixed_demo ;;
    1) run_experiment_1 ;;
    2) run_experiment_2 ;;
    3) run_experiment_3 ;;
    all)
        run_mixed_demo
        run_experiment_1
        run_experiment_2
        run_experiment_3
        ;;
    *)
        echo "Usage: $0 [mixed|1|2|3|all]" >&2
        exit 2
        ;;
esac

if [[ "${failed}" -ne 0 ]]; then
    echo "EXPERIMENTS FAILED" >&2
    exit 1
fi
echo "All selected experiments passed."
