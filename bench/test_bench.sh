#!/bin/env bash

cat <<EOF > bench.txt
    PUT /etc/hosts    P00000L5
    GET /etc/yum.conf P00000L5
    GET /etc/vimrc    P00001L5
    GET /etc/bashrc   P00000L5
    GET /etc/passwd   Q00002L6
EOF

./pho-bench dd bench.txt

