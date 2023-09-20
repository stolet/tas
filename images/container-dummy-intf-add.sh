#!/usr/bin/env bash

# Creates a dummy networking interface with an ip and mac address

set -o errexit

if [ "$#" -ne 4 ]; then
    echo "Illegal number of parameters"
    echo "usage:"
    echo "[interface, ip, mac, container_name]"
    exit
fi

# The name of the networking interface
interface=$1
# Ip of the interface
ip=$2
# The MAC address of the interface
mac=$3
container_name=$4


sudo modprobe dummy
PID=$(sudo docker inspect -f '{{.State.Pid}}' $container_name)

sudo nsenter -t $PID -n ip link add $interface type dummy
echo "Added interface $interface"

sudo nsenter -t $PID -n ifconfig eth0 hw ether $mac
echo "Added MAC address $mac to $interface"

sudo nsenter -t $PID -n ip addr add $ip brd + dev $interface label $interface:0
echo "Added ip address $ip to interface"

sudo nsenter -t $PID -n ip link set dev eth0 up
echo "$interface is up"