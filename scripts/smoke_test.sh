#!/usr/bin/env bash
set -euo pipefail

server_pid=""
cleanup() {
    if [[ -n "${server_pid}" ]]; then
        kill -TERM "${server_pid}" 2>/dev/null || true
        wait "${server_pid}" 2>/dev/null || true
    fi
}
trap cleanup EXIT

./mock_server > /tmp/cinema_mock_server.log 2>&1 &
server_pid=$!

for _ in {1..50}; do
    if [[ -e /dev/mqueue/cinema_request ]]; then
        break
    fi
    sleep 0.05
done

client_output="$(printf 'LIST\nSTATUS 10\nRESERVE 10\nSTATUS 10\nCANCEL 10\nQUIT\n' | ./client 1)"

grep -Fq "SUCCESS: Seat 10 is available." <<<"${client_output}"
grep -Fq "SUCCESS: Seat 10 reserved successfully." <<<"${client_output}"
grep -Fq "SUCCESS: Seat 10 is reserved by Client 1." <<<"${client_output}"
grep -Fq "SUCCESS: Seat 10 reservation cancelled." <<<"${client_output}"
grep -Fq "SUCCESS: Client session closed." <<<"${client_output}"

printf '%s\n' "Smoke test passed."
