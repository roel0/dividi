#!/bin/bash
sudo socat PTY,link=/dev/ttyS10 PTY,link=/dev/ttyS11 &
sleep 1
sudo ./build/test/serial_test || exit 1
sudo ./build/test/queue_test || exit 1

echo "##########################"
echo "    TEST SUCCESSFUL"
echo "##########################"
