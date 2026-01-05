#!/bin/bash

PORT="/dev/ttyUSB0"
BAUD="115200"
CHUNKS=16
CHUNK_SIZE=$((0x40000)) 

i=0
while [ $i -lt $CHUNKS ]
do
    START_ADDR=$(printf "0x%X" $((i * CHUNK_SIZE)))
    FILENAME="part_${i}.bin"
    
    echo ""
    echo "-------------------------------------------------------"
    echo ">> SEGMENT $i / 15 (Address: $START_ADDR)"
    echo "-------------------------------------------------------"
    echo "1. Hold GPIO0 to ground (GND)"
    echo "2. Press RESET"
    read -p "3. Press [ENTER] to extract..."

    esptool.py --port $PORT --baud $BAUD read_flash $START_ADDR $CHUNK_SIZE $FILENAME
    
    if [ $? -eq 0 ]; then
        echo "OK: $FILENAME extracted."
        ((i++))
    else
        echo "FAILED on segment $i. Will retry this segment."
        echo "Check your cables and power supply before continuing."
        sleep 1
    fi
done

echo ""
echo "--- MERGING ---"
cat \
    part_0.bin part_1.bin part_2.bin part_3.bin part_4.bin part_5.bin part_6.bin part_7.bin \
    part_8.bin part_9.bin part_10.bin part_11.bin part_12.bin part_13.bin part_14.bin part_15.bin \
    > backup_full.bin
echo "Done: backup_full.bin ready."
