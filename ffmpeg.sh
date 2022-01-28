#!/usr/bin/env bash

docker run --rm \
  --device /dev/dri:/dev/dri -v $(pwd):$(pwd) -w $(pwd) jrottenberg/ffmpeg:4.4.1-vaapi2004 \
  -hwaccel vaapi -hwaccel_output_format vaapi -hwaccel_device /dev/dri/renderD128 \
  "$@"