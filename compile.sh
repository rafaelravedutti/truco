#!/bin/bash
for i in $(seq 1 4); do
    gcc -o "mac${i}" "mac${i}.c" -Wall
done
