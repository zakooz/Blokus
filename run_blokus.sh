#!/bin/bash

echo "-------------- Compiling --------------"
make all
echo "-------------- Initializing fakefpga --------------"
x-terminal-emulator -e "./fakefpga master"
echo "-------------- Initializing blokus host --------------"
x-terminal-emulator -e "./blokus_host.sh"
echo "-------------- Initializing browser --------------"
URL="http://localhost:11000"
if which xdg-open > /dev/null
then
  xdg-open $URL
elif which gnome-open > /dev/null
then
  gnome-open $URL
fi
echo "-------------- Done --------------"
