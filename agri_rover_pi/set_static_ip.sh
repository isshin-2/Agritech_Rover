#!/bin/bash

# Configuration
STATIC_IP="10.48.169.241/24"
GATEWAY="10.48.169.1"
DNS="8.8.8.8"

echo "Detecting active WiFi connection..."
# Get the connection name for wlan0
CONN_NAME=$(nmcli -t -f NAME,DEVICE connection show --active | grep wlan0 | cut -d: -f1)

if [ -z "$CONN_NAME" ]; then
    echo "❌ No active WiFi connection found on wlan0."
    echo "Please connect to WiFi first."
    exit 1
fi

echo "Found connection: '$CONN_NAME'"
echo "Setting Static IP: $STATIC_IP"

# Configure Static IP
sudo nmcli con mod "$CONN_NAME" ipv4.addresses $STATIC_IP
sudo nmcli con mod "$CONN_NAME" ipv4.gateway $GATEWAY
sudo nmcli con mod "$CONN_NAME" ipv4.dns $DNS
sudo nmcli con mod "$CONN_NAME" ipv4.method manual

echo "Applying changes..."
sudo nmcli con up "$CONN_NAME"

echo "✅ Done! IP set to $STATIC_IP"
echo "If disconnected, SSH back into: ssh agritech@10.48.169.241"
