#!/usr/bin/env bash
# Every item the assignment (section 7) wants in README.md must be present.
# Deeper explanations may live in docs/ (need_any); the how-to items must be in README.md itself.
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

status=0
need() { # need <description> <fixed text>: must be in README.md
    if grep -Fq -- "$2" README.md; then
        echo "ok: $1"
    else
        echo "MISSING in README.md: $1 (looking for: $2)" >&2
        status=1
    fi
}
need_any() { # need_any <description> <fixed text>: README.md or any docs/*.md
    if grep -Fq -- "$2" README.md docs/*.md; then
        echo "ok: $1"
    else
        echo "MISSING in README.md and docs/: $1 (looking for: $2)" >&2
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
need_any "critical section explanation" "Critical Section"
need_any "why a message queue alone is not enough" "Message Queue เพียงอย่างเดียว"
need_any "log explanation" "RACE DETECTED"

for text in "ยังไม่อยู่ในส่วนนี้" "mock_server"; do
    if grep -Fq -- "${text}" README.md docs/*.md; then
        echo "README/docs still mention stale text: ${text}" >&2
        status=1
    fi
done

# Relative links to files in the repo must point at something that exists.
while IFS= read -r link; do
    target="${link%%#*}"
    case "${target}" in
        http*|"") continue ;;
    esac
    if [[ ! -e "${target}" ]]; then
        echo "BROKEN LINK in README.md: ${link}" >&2
        status=1
    fi
done < <(grep -o ']([^)]*)' README.md | sed 's/^](//; s/)$//')
while IFS= read -r file; do
    while IFS= read -r link; do
        target="${link%%#*}"
        case "${target}" in
            http*|"") continue ;;
        esac
        if [[ ! -e "docs/${target}" ]]; then
            echo "BROKEN LINK in ${file}: ${link}" >&2
            status=1
        fi
    done < <(grep -o ']([^)]*)' "${file}" | sed 's/^](//; s/)$//')
done < <(ls docs/*.md)

exit "${status}"
