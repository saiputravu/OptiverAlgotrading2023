#!/usr/bin/env bash

if [[ $# -ne 1 ]]; then
  echo "Usage: $0 [ Release | Debug ]"
  exit
fi

cmake -DCMAKE_BUILD_TYPE=$1 -B build
cmake --build build --config $1

cp build/autotrader .
