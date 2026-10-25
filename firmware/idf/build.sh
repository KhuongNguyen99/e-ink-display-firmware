#!/bin/sh
set -eu
export CCACHE_DIR=/build/.ccache
test "$(git -C /opt/esp/idf rev-parse HEAD)" = ac5d805d0eda75442dd17cfea5baaca9748f8215
test "$(git -C /arduino rev-parse HEAD)" = 9b4622dc40e3cd0a9220d0e44b3291ea28500940
idf.py -D SDKCONFIG=/project/sdkconfig -D ARDUINO_COMPONENT_PATH=/arduino -D "INVENTORY_COUNTER_PANEL=$1" -B /build build
python /project/export-build.py
