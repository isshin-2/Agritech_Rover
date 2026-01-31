#!/bin/bash

# Define the static IP details
INTERFACE="wlan0"
STATIC_IP="10.48.169.241/24"
ROUTER_IP="10.48.169.1"
DNS_IP="8.8.8.8"

echo "Setting static IP for $INTERFACE to $STATIC_IP..."

# Backup the original config
sudo cp /etc/dhcpcd.conf /etc/dhcpcd.conf.backup

# Append configuration
echo "" | sudo tee -a /etc/dhcpcd.conf
echo "interface $INTERFACE" | sudo tee -a /etc/dhcpcd.conf
echo "static ip_address=$STATIC_IP" | sudo tee -a /etc/dhcpcd.conf
echo "static routers=$ROUTER_IP" | sudo tee -a /etc/dhcpcd.conf
echo "static domain_name_servers=$DNS_IP" | sudo tee -a /etc/dhcpcd.conf

echo "Configuration added."
echo "Restarting networking service..."
sudo systemctl restart dhcpcd

echo "Done! IP should now be fixed to $STATIC_IP"
