param([string]$Image = 'espressif/idf:v5.3.2',
      [string]$FirmwareRoot = (Split-Path $PSScriptRoot))
$ErrorActionPreference = 'Stop'
$testRoot = Join-Path $env:TEMP ('inventory-counter-firmware-tests-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot | Out-Null
Copy-Item -LiteralPath (Join-Path $FirmwareRoot 'inventory_counter_label'), $PSScriptRoot -Destination $testRoot -Recurse
try {
    # Non-PIE avoids intermittent ASan shadow-memory startup collisions under Docker Desktop.
    $testScript = @'
set -e
cp -a /opt/esp/idf/components/mbedtls/mbedtls /tmp/mbedtls
cmake=/opt/esp/tools/cmake/3.30.2/bin/cmake
timeout 30 $cmake -S /tmp/mbedtls -B /tmp/mbedtls-build -DENABLE_TESTING=OFF -DENABLE_PROGRAMS=OFF -DGEN_FILES=OFF -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-pie' -DCMAKE_EXE_LINKER_FLAGS='-no-pie' >/tmp/crypto-build.log 2>&1 || { tail -50 /tmp/crypto-build.log; exit 1; }
timeout 120 $cmake --build /tmp/mbedtls-build --target mbedcrypto -j4 >>/tmp/crypto-build.log 2>&1 || { tail -50 /tmp/crypto-build.log; exit 1; }
gcc -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-pie -DINVENTORY_COUNTER_TEST_HKDF_FALLBACK -I/tmp/mbedtls/include -c /src/inventory_counter_label/hkdf_fallback.c -o /tmp/hkdf-fallback.o
crypto_flags='-I/tmp/mbedtls/include /tmp/hkdf-fallback.o /tmp/mbedtls-build/library/libmbedcrypto.a'
g++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-pie -no-pie /src/tests/protocol_test.cpp -o /tmp/protocol-test
timeout 15 /tmp/protocol-test
mkdir /tmp/cjson
for source in cJSON.c cJSON.h; do
    git -C /opt/esp/idf/components/json/cJSON show v1.7.15:$source > /tmp/cjson/$source
done
gcc -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-pie -I/tmp/cjson -c /tmp/cjson/cJSON.c -o /tmp/cjson/cJSON.o
g++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-pie -no-pie -Wl,--wrap=sscanf,--wrap=__isoc99_sscanf -I/tmp/cjson /src/tests/ble_json_test.cpp /tmp/cjson/cJSON.o -o /tmp/ble-json-test
timeout 15 /tmp/ble-json-test
if [ -f /src/tests/power_policy_test.cpp ]; then
    g++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-pie -no-pie /src/tests/power_policy_test.cpp -o /tmp/power-policy-test
    timeout 15 /tmp/power-policy-test
fi
if [ -f /src/tests/light_sleep_wake_test.cpp ]; then
    g++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-pie -no-pie -I/src/tests/runtime /src/tests/light_sleep_wake_test.cpp -o /tmp/light-sleep-wake-test
    timeout 15 /tmp/light-sleep-wake-test
fi
if [ -f /src/tests/power_runtime_test.cpp ]; then
    g++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-pie -no-pie -I/src/tests/runtime -I/tmp/cjson /src/tests/power_runtime_test.cpp /tmp/cjson/cJSON.o -o /tmp/power-runtime-test
    timeout 15 /tmp/power-runtime-test
    g++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-pie -no-pie -DINVENTORY_COUNTER_TEST_NO_PM -I/src/tests/runtime -I/tmp/cjson /src/tests/power_runtime_test.cpp /tmp/cjson/cJSON.o -o /tmp/power-runtime-fallback-test
    timeout 15 /tmp/power-runtime-fallback-test
fi
g++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-pie -no-pie -DINVENTORY_COUNTER_SSD1680 -I/src/tests/runtime -I/tmp/cjson /src/tests/unbonded_test.cpp /tmp/cjson/cJSON.o $crypto_flags -o /tmp/unbonded-test
timeout 15 /tmp/unbonded-test
g++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-pie -no-pie -DINVENTORY_COUNTER_SSD1680 -I/src/tests/runtime -I/tmp/cjson /src/tests/sleep_test.cpp /tmp/cjson/cJSON.o $crypto_flags -o /tmp/sleep-test
timeout 30 /tmp/sleep-test
g++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-pie -no-pie -DINVENTORY_COUNTER_SSD1680 -I/src/tests/runtime -I/tmp/cjson /src/tests/find_tag_test.cpp /tmp/cjson/cJSON.o $crypto_flags -o /tmp/find-tag-test
timeout 30 /tmp/find-tag-test
g++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-pie -no-pie -DINVENTORY_COUNTER_SSD1680 -I/src/tests/runtime -I/tmp/cjson /src/tests/provisioning_test.cpp /tmp/cjson/cJSON.o $crypto_flags -o /tmp/provisioning-test
timeout 30 /tmp/provisioning-test /src/tests/provisioning_vector.json
for profile in SSD1680 JD79661; do
    g++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-pie -no-pie -DINVENTORY_COUNTER_$profile -I/src/tests /src/inventory_counter_label/panel.cpp /src/tests/panel_test.cpp -o /tmp/panel-test
    timeout 15 /tmp/panel-test
done
if [ -f /src/tests/panel_power_test.cpp ]; then
    g++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-pie -no-pie -DINVENTORY_COUNTER_SSD1680 -I/src/tests /src/inventory_counter_label/panel.cpp /src/tests/panel_power_test.cpp -o /tmp/panel-power-test
    timeout 15 /tmp/panel-power-test
fi
'@
    docker run --rm --env 'ASAN_OPTIONS=abort_on_error=1' --mount "type=bind,src=$testRoot,dst=/src,readonly" --entrypoint sh $Image -c $testScript
    if ($LASTEXITCODE) { throw 'Firmware host tests failed.' }
} finally {
    $resolvedTestRoot = [IO.Path]::GetFullPath($testRoot)
    $resolvedTemp = [IO.Path]::GetFullPath($env:TEMP).TrimEnd('\') + '\'
    if (-not $resolvedTestRoot.StartsWith($resolvedTemp, [StringComparison]::OrdinalIgnoreCase) -or
        (Split-Path $resolvedTestRoot -Leaf) -notlike 'inventory-counter-firmware-tests-*') {
        throw 'Refusing to remove an unexpected test directory.'
    }
    Remove-Item -LiteralPath $resolvedTestRoot -Recurse -Force
}
