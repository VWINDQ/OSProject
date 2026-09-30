#!/usr/bin/env bash
# Every item the assignment (section 7) wants in README.md must be present.
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

status=0
need() { # need <description> <fixed text>
    if grep -Fq -- "$2" README.md; then
        echo "ok: $1"
    else
        echo "MISSING in README.md: $1 (looking for: $2)" >&2
        status=1
    fi
}

need "how to build the Docker image" "docker build -t cinema-reservation ."
need "how to run the container" "docker run -dit --name cinema"
need "how to start the server" "docker exec -it cinema ./server"
need "how to start several clients" "docker exec -it cinema ./client 5"
need "message queue type" "POSIX Message Queue"
need "request queue name" "/cinema_request"
need "response queue name" "/cinema_client_<client_id>_<pid>"
need "supported commands" "STATUS <seat_id>"
need "race experiment command" "bash scripts/experiments.sh 2"
need "sync switch" "./server nosync 3"
need "sync switch (on)" "./server sync 3"
need "experiment 1" "Experiment 1"
need "experiment 2" "Experiment 2"
need "experiment 3" "Experiment 3"
need "critical section explanation" "Critical Section"
need "why a message queue alone is not enough" "Message Queue เพียงอย่างเดียว"
need "log explanation" "RACE DETECTED"

if grep -Fq "ยังไม่อยู่ในส่วนนี้" README.md; then
    echo "README still says the real server is missing" >&2
    status=1
fi
if grep -Fq "mock_server" README.md; then
    echo "README still mentions mock_server, which no longer exists" >&2
    status=1
fi
exit "${status}"
