#!/bin/bash
set -e

# TerraTec DMX 6Fire USB Firmware Downloader
# This script downloads the firmware from a known mirror and installs it to /lib/firmware/6fire/

FIRMWARE_URL="ftp://195.220.108.108/linux/Mandriva/devel/cooker/x86_64/media/non-free/release/6fire-firmware-1.23.0.02-1-mdv2012.0.noarch.rpm"
RPM_FILE="6fire-firmware.rpm"
TEMP_DIR=$(mktemp -d)

echo "Checking dependencies..."
for cmd in curl rpm2cpio cpio; do
    if ! command -v $cmd &> /dev/null; then
        echo "Error: '$cmd' is required but not installed."
        echo "Please install it using your package manager (e.g., sudo dnf install $cmd)"
        exit 1
    fi
done

echo "Downloading firmware RPM..."
curl -o "$TEMP_DIR/$RPM_FILE" "$FIRMWARE_URL"

echo "Extracting firmware..."
pushd "$TEMP_DIR" > /dev/null
rpm2cpio "$RPM_FILE" | cpio -idmv --quiet
popd > /dev/null
echo "Installing firmware to /lib/firmware/6fire/..."
if [ ! -d /lib/firmware/6fire ]; then
    echo "Creating directory /lib/firmware/6fire..."
    sudo mkdir -p /lib/firmware/6fire
fi

# Find the directory containing the firmware files (dmx6firel2.ihx)
FOUND_DIR=$(find "$TEMP_DIR" -name "dmx6firel2.ihx" -exec dirname {} \; | head -n 1)

if [ -n "$FOUND_DIR" ]; then
    echo "Found firmware in: $FOUND_DIR"
    sudo cp -v "$FOUND_DIR/"* /lib/firmware/6fire/
else
    echo "Error: Could not find extracted firmware files."
    rm -rf "$TEMP_DIR"
    exit 1
fi

echo "Cleaning up..."
rm -rf "$TEMP_DIR"

echo "Done! Firmware successfully installed."
echo "Please reconnect your DMX 6Fire USB device."