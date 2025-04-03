#!/bin/sh

#  Copyright (C) 2025 by Abhirath Koushik

#  Redistribution, modification or use of this software in source or binary
#  forms is permitted as long as the files maintain this copyright. Users are
#  permitted to modify this and use it to learn about the field of embedded
#  software. Abhirath Koushik and the University of Colorado are not liable for
#  any misuse of this material.

#  @file    aesdsocket-start-stop.sh
#  @brief   This script is used to start and stop the aesdchar program

#  @author  Abhirath Koushik
#  @date    03-16-2025

case "$1" in
    start)
        echo "Starting aesdchar program!"
        aesdchar_load
        ;;
    stop)
        echo "Stopping aesdchar program!"
        aesdchar_unload
        ;;
    *)
        echo "Usage $0 {start | stop}"
    exit 1
esac
