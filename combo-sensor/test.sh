#!/bin/sh

echo "Load i2c-envcombo-sim.ko"
insmod i2c-envcombo-sim.ko || { echo "[ERROR] Failed to load simulator module"; exit 1; }

echo "Load cs-driver.ko"
insmod cs-driver.ko 

for folder in /sys/bus/iio/devices/iio:device*; do
    if grep -q "combo-sensor" "$folder/name"; then
        dev="$folder"
        break
    fi
done

if [ -z "$dev" ]; then
    exit 1
fi

echo "$dev is located"

temp_ns=$(cat "$dev/in_temp_raw")
temp_scale=$(cat "$dev/in_temp_scale")
echo "Temperature $temp_ns:$temp_scale"

hum_ns=$(cat "$dev/in_humidityrelative_raw")
hum_scale=$(cat "$dev/in_humidityrelative_scale")

echo "Humidity $hum_ns:hum_scale$"
