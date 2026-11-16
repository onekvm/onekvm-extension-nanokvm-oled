#!/bin/sh
# Explicit PCIe hardware regression: read the device document before running.
# Requires an idle Core (or a separate test bus) and the enabled OLED service.
set -eu
target=${1:?usage: test-idle-device.sh root@device /installed/bin/onekvm-nanokvm-oled [test-bus-address]}
binary=${2:?missing installed OLED binary}
test_bus=${3:-unix:path=/run/dbus/system_bus_socket}
ssh -o BatchMode=yes "$target" sh -s -- "$binary" "$test_bus" <<'REMOTE'
set -eu
binary=$1
test_bus=$2
case "$(onekvm-extension detail nanokvm-oled)" in
    *'"running":true'*) ;;
    *) echo 'OLED must be running before this test' >&2; exit 1 ;;
esac
scratch=$(mktemp -d /tmp/onekvm-oled-idle.XXXXXX)
pid=
cleanup() {
    if [ -n "$pid" ]; then kill "$pid" 2>/dev/null || true; wait "$pid" 2>/dev/null || true; fi
    cat "$scratch/runtime.log" 2>/dev/null || true
    rm -rf "$scratch"
    onekvm-extension start nanokvm-oled
}
trap cleanup EXIT
trap 'exit 1' HUP INT TERM
cat > "$scratch/config.json" <<'JSON'
{"display_enabled":true,"burn_in_protection":true,"rotation":0,"sleep_seconds":7,"layout_json":"","line1":"{c_idle}","line2":"","line3":"","line4":"","variables_json":"{\"c_idle\":\"IDLE TEST\"}"}
JSON
seq() { devmem 0x8fff0004 32; }
cmd() { devmem 0x8fff000c 32; }
expect_cmd() {
    actual=$(cmd)
    if [ "$actual" != "$1" ]; then
        echo "expected OLED command $1, got $actual" >&2
        exit 1
    fi
}
onekvm-extension stop nanokvm-oled
before=$(seq)
DBUS_SYSTEM_BUS_ADDRESS=$test_bus "$binary" --config "$scratch/config.json" --shift-interval 2 > "$scratch/runtime.log" 2>&1 </dev/null &
pid=$!
ready=false
for attempt in 1 2 3 4 5 6 7 8 9 10; do
    sleep 0.2
    kill -0 "$pid"
    if [ "$(seq)" != "$before" ]; then ready=true; break; fi
done
[ "$ready" = true ]
expect_cmd 0x00000001
initial=$(seq)
sleep 2.5
expect_cmd 0x00000001
[ "$(seq)" != "$initial" ]
echo 'Protection shifted the frame before the idle deadline'
sleep 5
expect_cmd 0x00000005
asleep=$(seq)
sleep 2.5
[ "$(seq)" = "$asleep" ]
echo 'Screen turned off after 7 seconds and stayed off across a shift interval'
DBUS_SYSTEM_BUS_ADDRESS=$test_bus busctl call org.onekvm.NanoKvmOled1 /org/onekvm/NanoKvmOled1 org.onekvm.NanoKvmOled1 VariableUpdate ss c_idle WAKE
sleep 0.5
expect_cmd 0x00000001
sleep 2.5
expect_cmd 0x00000001
sleep 5
expect_cmd 0x00000005
echo 'Visible content woke the screen and received a new idle deadline'
REMOTE
