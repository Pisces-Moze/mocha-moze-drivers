#!/bin/sh
# Use the validated stock OPP and its driver-managed voltage while displaying.
# This does not alter the frequency table, voltage table, or PMU firmware.
set -eu
for node in /sys/kernel/debug/dri/*/pstate; do
    if grep -q '0f: core 852 MHz' "$node"; then
        pstate=$node
        break
    fi
done
test -n "${pstate:-}"
for node in /sys/class/thermal/thermal_zone*; do
    if [ "$(cat "$node/type")" = gpu-thermal ]; then
        thermal=$node/temp
        break
    fi
done
test -n "${thermal:-}"
cleanup() { printf 'auto\n' > "$pstate"; }
trap cleanup EXIT
trap 'exit 0' INT TERM
previous=
hot=0
low_voltage=0
ready=0
gauge=/sys/class/power_supply/bq27520g4-0
charger=/sys/class/power_supply/bq24190-charger
while :; do
    temperature=$(cat "$thermal")
    [ "$temperature" -lt 75000 ] || hot=1
    [ "$temperature" -gt 68000 ] || hot=0
    display_on=0
    for node in /sys/class/drm/card*-DSI-1/dpms; do
        if [ "$(cat "$node")" = On ]; then display_on=1; fi
    done
    # Keep initialization at automatic clocks until the compositor is running.
    if [ "$display_on" = 1 ] && pidof niri >/dev/null; then
        [ "$ready" -ge 3 ] || ready=$((ready + 1))
    else
        ready=0
    fi
    voltage=$(cat "$gauge/voltage_now")
    [ "$voltage" -ge 3550000 ] || low_voltage=1
    [ "$voltage" -lt 3750000 ] || low_voltage=0
    input=$(cat "$charger/input_current_limit")
    online=$(cat "$charger/online")
    if [ "$display_on" = 0 ] || [ "$ready" -lt 3 ]; then
        requested=auto
    elif [ "$low_voltage" = 1 ]; then
        requested=08
    elif [ "$hot" = 1 ]; then
        requested=0a
    elif [ "$online" != 1 ] || [ "$input" -lt 900000 ]; then
        # The measured 500 mA computer connection sagged to 3.53 V at 852 MHz.
        requested=0a
    else
        requested=0f
    fi
    if [ "$previous" != "$requested" ]; then
        printf '%s\n' "$requested" > "$pstate"
        printf 'MOCHA_GPU_POLICY state=%s temperature=%s voltage=%s input_ua=%s\n' "$requested" "$temperature" "$voltage" "$input"
        previous=$requested
    fi
    sleep 2
done
