#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Inspect or capture a short stream from a selected Mocha camera capture node.
set -eu

case $# in
    0)
        printf 'Mocha camera inventory (%s)\n' "$(uname -r)"
        for bus in /sys/bus/i2c/devices/*-0010 /sys/bus/i2c/devices/*-0036 /sys/bus/i2c/devices/*-000c; do
            [ -r "$bus/name" ] || continue
            printf '%s: %s\n' "${bus##*/}" "$(cat "$bus/name")"
        done
        for media in /dev/media[0-9]*; do
            [ -e "$media" ] || continue
            media-ctl -d "$media" -p
        done
        for video in /sys/class/video4linux/video[0-9]*; do
            [ -r "$video/name" ] || continue
            printf '%s: %s\n' "${video##*/}" "$(cat "$video/name")"
        done
        ;;
    2)
        sensor=$1
        node=$2
        case "$sensor" in
            front) expected=ov5693 ;;
            rear) expected=imx179 ;;
            *) echo 'Expected front or rear.' >&2; exit 2 ;;
        esac
        case "$node" in /dev/video[0-9]*) ;; *) exit 2 ;; esac
        [ -c "$node" ] || exit 2
        checker=$(dirname "$0")/check-media-route.py
        matched=no
        for media in /dev/media[0-9]*; do
            [ -e "$media" ] || continue
            topology=$(media-ctl -d "$media" -p) || continue
            if printf '%s\n' "$topology" | python3 "$checker" "$expected" "$node"; then
                matched=yes
                break
            fi
        done
        if [ "$matched" != yes ]; then
            echo "No enabled $expected route to $node. Inspect the graph before streaming." >&2
            exit 1
        fi
        caps=$(v4l2-ctl -d "$node" -D)
        case "$caps" in
            *'Memory-to-Memory'*|*'Memory to Memory'*) echo 'Decoder node, not a camera.' >&2; exit 1 ;;
            *'Video Capture'*) : ;;
            *) echo 'Node does not expose video capture.' >&2; exit 1 ;;
        esac
        printf 'Testing %s (%s), %s\n' "$sensor" "$expected" "$node"
        v4l2-ctl -d "$node" --get-fmt-video
        timeout 10s v4l2-ctl -d "$node" --stream-mmap=4 --stream-count=8 --stream-to=/dev/null
        ;;
    *) echo 'Usage: mocha-camera-probe.sh [front|rear /dev/videoN]' >&2; exit 2 ;;
esac
