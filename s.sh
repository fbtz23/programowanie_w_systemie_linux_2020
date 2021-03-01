#!/bin/bash
for ((c=0; c<100; c++))
do
  ./konsument -c 1 -p 1 -d 1 8888 &
  if [ $(($c%100)) -eq 0 ]; then
  sleep 1
  fi
done
