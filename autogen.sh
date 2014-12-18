#!/bin/bash

dir=$(dirname $(readlink -m $0))
mkdir -p $dir/autotools $dir/autotools/m4

autoreconf --install --force
