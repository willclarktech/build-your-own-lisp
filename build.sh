#!/usr/bin/env bash

cc -std=c99 -Wall lib/mpc.c "$1.c" -ledit -o "$1.o"
