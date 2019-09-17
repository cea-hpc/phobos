#!/bin/bash

set -e

cur_dir="$(dirname $(readlink -m ${BASH_SOURCE[0]}))"
cd "$cur_dir/.."

docker build -f "$cur_dir/Dockerfile" . -t phobos
# Need privileges for process tracing (and in the future for tape access)
docker run --privileged -v /run/systemd/journal/dev-log:/dev/log phobos
