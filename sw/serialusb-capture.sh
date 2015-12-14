#!/bin/bash

CAPTURE="$PWD/capture.pcap"

function die() {
  echo "$1"
  exit -1
}

trap ctrl_c INT

function ctrl_c() {
  pkill -SIGINT serialusb
}

test $EUID -ne 0 && die "This script must be run as root!"

#
# USB to UART adapter selection.
#

DEVICES="$(ls /dev/ttyUSB* 2> /dev/null)"

test -z "$DEVICES" && die "No USB to UART adapter detected!"

echo Select a USB to UART adapter:

INDEX=0
for DEV in $DEVICES
do
  echo $INDEX: "$DEV"
  DEVS[$INDEX]="$DEV"
  INDEX=$((INDEX+1))
done

read -r SELECTED

(test -z "$SELECTED" || test "$SELECTED" -lt 0 || test "$SELECTED" -ge $INDEX) && die "Invalid value."

echo Selected: "${DEVS[$SELECTED]}"

#
# Usbmon insertion.
#

! modprobe usbmon 2> /dev/null && die "Failed to load usbmon."

read

COUNT=0
while [ "$COUNT" -lt 50 ]
do
  test -c /dev/usbmon0 && break
  COUNT=$((COUNT+1))
  sleep .1
done
test "$COUNT" -eq 50 && die "usbmon0 was not found."

#
# Kill running tcpdump instances.
#

for PID in $(pgrep tcpdump)
do
  for FILE in /proc/"$PID"/fd/*
  do
    test "$(readlink "$FILE")" == "$CAPTURE" && kill "$PID"
  done
done

#
# Check capture file presence.
#

if [ -f "$CAPTURE" ]
then
  echo Overwrite "$CAPTURE"? [y/n]
  read -r LINE
  if [ "$LINE" != "y" ]
  then
    echo Aborted. && exit 0
  fi
  ! rm "$CAPTURE" 2> /dev/null && die "Failed to remove $CAPTURE".
fi

#
# Start tcpdump.
#

tcpdump -i usbmon0 -w "$CAPTURE" 2> /dev/null &

PID=$!

#
# Wait up to 5 seconds for the capture file to be created.
#

COUNT=0
while [ "$COUNT" -lt 50 ]
do
  test -f "$CAPTURE" && break
  COUNT=$((COUNT+1))
  sleep .1
done

#
# Check that tcpdump is running.
#

test -d /proc/$PID || die "Failed to start tcpdump."

#
# Start the proxy.
#

serialusb "${DEVS[$SELECTED]}"

RESULT=$?

#
# Stop tcpdump.
#

kill -SIGINT $PID 2> /dev/null

#
# Display result.
#

test "$RESULT" -ne 0 && die "Capture failed."

echo
echo The capture file was saved into "$CAPTURE".
echo It can be opened using wireshark!
