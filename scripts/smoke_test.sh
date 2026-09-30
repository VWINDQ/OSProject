#!/usr/bin/env bash
# End-to-end tests of ./server with ./client and ./raw_request.
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

QUEUE_FILE=/dev/mqueue/cinema_request
work_dir="$(mktemp -d)"
server_pid=""
current_log=""

stop_server() {
    local status=0

    if [[ -n "${server_pid}" ]]; then
        kill -TERM "${server_pid}" 2>/dev/null || true
        wait "${server_pid}" 2>/dev/null || status=$?
        server_pid=""
    fi
    return "${status}"
}

cleanup() {
    stop_server || true
    rm -rf "${work_dir}"
}
trap cleanup EXIT

fail() {
    echo "FAIL: $*" >&2
    if [[ -n "${current_log}" && -f "${current_log}" ]]; then
        echo "--- ${current_log} ---" >&2
        cat "${current_log}" >&2
    fi
    exit 1
}

# wait_for <what> <command...>: poll for up to 5 seconds
wait_for() {
    local what="$1"
    local attempt

    shift
    for attempt in $(seq 1 100); do
        if "$@"; then
            return 0
        fi
        sleep 0.05
    done
    fail "timed out waiting for ${what}"
}

log_has() { grep -Fq -- "$1" "${current_log}"; }

# start_server <log-name> <server arguments...>
# Set KEEP_QUEUE=1 to leave a stale queue in place.
start_server() {
    local name="$1"

    shift
    current_log="${work_dir}/${name}.log"
    if [[ "${KEEP_QUEUE:-0}" != "1" ]]; then
        rm -f "${QUEUE_FILE}"
    fi
    ./server "$@" > "${current_log}" 2>&1 &
    server_pid=$!
    wait_for "server to start" log_has "started:"
}

expect() { # expect <text> <needle> <label>
    grep -Fq -- "$2" <<<"$1" || fail "$3: expected '$2' in: $1"
    echo "  ok: $3"
}

run_client() { # run_client <client_id> <command...>: one command, then QUIT
    local id="$1"

    shift
    printf '%s\nQUIT\n' "$*" | ./client "${id}" 2>&1
}

echo "== A. functional behaviour (sync 3 workers, no delay)"
start_server functional sync 3 0 0

output="$(printf 'LIST\nSTATUS 10\nRESERVE 10\nSTATUS 10\nCANCEL 10\nQUIT\n' | ./client 1 2>&1)"
expect "${output}" "SUCCESS: Seats (- = available, Cn = reserved by Client n): 1:- 2:-" "LIST"
expect "${output}" "SUCCESS: Seat 10 is available." "STATUS on a free seat"
expect "${output}" "SUCCESS: Seat 10 reserved successfully." "RESERVE"
expect "${output}" "SUCCESS: Seat 10 is reserved by Client 1." "STATUS on a reserved seat"
expect "${output}" "SUCCESS: Seat 10 reservation cancelled." "CANCEL"
expect "${output}" "SUCCESS: Client session closed." "QUIT"

run_client 1 "RESERVE 5" > /dev/null
expect "$(run_client 2 'RESERVE 5')" "FAILED: Seat 5 is already reserved." "second client cannot reserve"
expect "$(run_client 2 'CANCEL 5')" "FAILED: Seat 5 belongs to another client." "cancel of another client's seat"
expect "$(run_client 1 'CANCEL 5')" "SUCCESS: Seat 5 reservation cancelled." "owner cancels"
expect "$(run_client 3 'CANCEL 5')" "FAILED: Seat 5 is not reserved." "cancel of a free seat"
expect "$(run_client 3 'RESERVE 0')" "Invalid input" "client rejects seat 0"
expect "$(run_client 3 'RESERVE 21')" "Invalid input" "client rejects seat 21"

echo "== A. malformed requests sent straight to the server"
expect "$(./raw_request 5 RESERVE 99 /cinema_client_5_1)" "FAILED: Invalid seat ID." "seat 99"
expect "$(./raw_request 5 FLY 1 /cinema_client_5_2)" "FAILED: Unsupported command." "unknown command"
expect "$(./raw_request 0 RESERVE 4 /cinema_client_0_3)" "FAILED: Invalid client ID." "client id 0"
expect "$(./raw_request -7 RESERVE 4 /cinema_client_7_4)" "FAILED: Invalid client ID." "negative client id"
./raw_request 5 RESERVE 4 /evil --no-reply > /dev/null
wait_for "bad queue name to be logged" log_has "invalid response queue name"
echo "  ok: reply queue outside /cinema_client_ is dropped"
./raw_request 5 RESERVE 4 /cinema_client_5_5 --short > /dev/null
wait_for "short message to be logged" log_has "unexpected size"
echo "  ok: truncated message is dropped"
./raw_request 5 STATUS 4 /cinema_client_5_777 --no-reply > /dev/null
wait_for "vanished client to be logged" log_has "cannot reply to Client-5"
echo "  ok: worker survives a client that has gone away"
expect "$(run_client 6 'STATUS 4')" "SUCCESS: Seat 4 is available." "seat 4 untouched by bad requests"

echo "== A. burst of 30 clients against a queue of depth 10"
mkdir "${work_dir}/burst"
pids=()
for id in $(seq 101 130); do
    (printf 'STATUS 1\nQUIT\n' | ./client "${id}" > "${work_dir}/burst/${id}.txt" 2>&1) &
    pids+=("$!")
done
wait "${pids[@]}"
ok_count="$(grep -l 'SUCCESS: Seat 1 is available.' "${work_dir}"/burst/*.txt | wc -l)"
[[ "${ok_count}" -eq 30 ]] || fail "only ${ok_count} of 30 burst clients got a reply"
echo "  ok: all 30 replies received"

echo "== B. clean shutdown"
kill -0 "${server_pid}" 2>/dev/null || fail "server died during traffic"
stop_server || fail "server exited with a non-zero status after SIGTERM"
[[ ! -e "${QUEUE_FILE}" ]] || fail "request queue was not removed"
log_has "Worker-1 processed" || fail "missing per-worker summary"
log_has "Resource  Status     Owner" || fail "missing final seat table"
log_has "stopped" || fail "missing stop message"
echo "  ok: exit 0, queue removed, summary and table logged"

echo "== C. stale queue after kill -9"
start_server crash sync 3 0 0
kill -KILL "${server_pid}"
wait "${server_pid}" 2>/dev/null || true
server_pid=""
[[ -e "${QUEUE_FILE}" ]] || fail "expected a stale queue after kill -9"
KEEP_QUEUE=1 start_server restart sync 3 0 0
expect "$(run_client 1 'RESERVE 2')" "SUCCESS: Seat 2 reserved successfully." "restarted server works"
stop_server || fail "restarted server did not stop cleanly"

echo "== D. SIGTERM while a worker is inside the random delay"
start_server midflight sync 3 300 300
(printf 'RESERVE 8\n' | ./client 7 > "${work_dir}/midflight.txt" 2>&1) &
client_pid=$!
wait_for "request to reach a worker" log_has "check Resource 8: AVAILABLE"
stop_server || fail "server exited with a non-zero status"
wait "${client_pid}"
expect "$(cat "${work_dir}/midflight.txt")" "SUCCESS: Seat 8 reserved successfully." "in-flight request still answered"
log_has "stopped" || fail "missing stop message"

echo "== E. bad command-line arguments"
for args in "nosync 0" "sync 33" "sync abc" "sync 3 500 50" "sync 3 50" "bogus" "sync 3 50 500 extra"; do
    # shellcheck disable=SC2086
    if ./server ${args} > /dev/null 2> "${work_dir}/usage.txt"; then
        fail "server accepted: ${args}"
    fi
    grep -Fq "Usage:" "${work_dir}/usage.txt" || fail "no usage text for: ${args}"
    echo "  ok: rejected '${args}'"
done

printf '%s\n' "Smoke test passed."
