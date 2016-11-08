#!/bin/bash
sudo socat PTY,link=/dev/ttyS10 PTY,link=/dev/ttyS11 &
sudo ./build/test/serial_test

echo "##########################"
echo "    TEST SUCCESSFUL"
echo "##########################"
