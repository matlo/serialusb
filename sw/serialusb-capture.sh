#!/bin/bash

trap ctrl_c INT

function ctrl_c() {
  pkill -SIGINT serialusb
}

test $EUID -ne 0 && echo "This script must be run as root!" 1>&2 && exit 1

test "$(ls /dev/ttyUSB0 | wc -l)" -eq 0 && echo No USB to UART adapter detected! && exit -1 

echo Select a USB to UART adapter:

INDEX=0
for DEV in /dev/ttyUSB*
do
  echo $INDEX: $DEV
  DEVS[$INDEX]=$DEV
  INDEX=$(($INDEX+1))
done

read SELECTED

(test "$SELECTED" -lt 0 || test "$SELECTED" -ge $INDEX) && echo Invalid value! && exit -1

echo Selected: ${DEVS[$SELECTED]}

modprobe usbmon 2> /dev/null

test -z "$(lsmod | grep usbmon)" && echo Failed to load usbmon! && exit -1 

if [ -f $PWD/capture.pcap ]
then
  echo Overwrite $PWD/capture.pcap? [y/n]
  read LINE
  if [ "$LINE" != "y" ]
  then
    exit 0
  fi
fi

for PID in `pgrep tcpdump`
do
  test -n "$(ls -l /proc/$PID/fd | grep $PWD/capture.pcap)" && kill $PID
done

tcpdump -i usbmon0 -w capture.pcap 2> /dev/null &

test -z "$(pgrep tcpdump)" && echo Failed to start tcpdump! && exit -1

serialusb ${DEVS[$SELECTED]}

pkill tcpdump

echo The capture file was saved into $PWD/capture.pcap.
echo It can be opened using wireshark!

