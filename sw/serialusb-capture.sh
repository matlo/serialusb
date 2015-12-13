#!/bin/bash

CAPTURE=capture.pcap

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

(test -z $SELECTED || test "$SELECTED" -lt 0 || test "$SELECTED" -ge $INDEX) && echo Invalid value! && exit -1

echo Selected: ${DEVS[$SELECTED]}

! modprobe usbmon 2> /dev/null && echo Failed to load usbmon! && exit -1

COUNT=0
while [ "$COUNT" -lt 5 ]
do
  test -c /dev/usbmon0 && break
  COUNT=$(($COUNT+1))
  sleep 1
done
test "$COUNT" -eq 5 && echo usbmon0 was not found! && exit -1

for PID in `pgrep tcpdump`
do
  test -n "$(ls -l /proc/$PID/fd | grep $PWD/$CAPTURE)" && kill $PID
done

if [ -f $CAPTURE ]
then
  echo Overwrite $PWD/$CAPTURE? [y/n]
  read LINE
  if [ "$LINE" != "y" ]
  then
    echo Aborted. && exit 0
  fi
  ! rm $CAPTURE 2> /dev/null && echo Failed to remove $CAPTURE! && exit -1
fi

tcpdump -i usbmon0 -w $CAPTURE 2> /dev/null &

PID=$!

COUNT=0
while [ "$COUNT" -lt 5 ]
do
  test -f $PWD/$CAPTURE && break
  COUNT=$(($COUNT+1))
  sleep 1
done

test -z "$(pgrep tcpdump)" && echo Failed to start tcpdump! && exit -1

serialusb ${DEVS[$SELECTED]}

RESULT=$?

kill -SIGINT $PID 2> /dev/null

test "$RESULT" -eq 0 && echo Failed to start the proxy! && exit -1

echo The capture file was saved into $PWD/$CAPTURE.
echo It can be opened using wireshark!
