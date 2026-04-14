#!/bin/bash
set -euo pipefail

if [ "$#" -lt 2 ]; then
  echo "Usage: $0 <image1> <image2> [extra face_engine_test args]"
  exit 1
fi

IMAGE1="$1"
IMAGE2="$2"
shift 2

./face_engine_test "$IMAGE1" "$IMAGE2" "$@"
