#!/usr/bin/env bash

if [ "$#" -ne 7 ]; then
    echo "Illegal number of parameters"
    echo "usage:"
    echo "[br_name veth_name_bridge veth_name_container n_queues container_name veth_bridge_ip veth_container_ip]"
    exit
fi

br_name=$1
veth_name_bridge=$2
veth_name_container=$3
n_queues=$4
container_name=$5
veth_bridge_ip=$6
veth_container_ip=$7

sudo ip link add $veth_name_container numrxqueues $n_queues numtxqueues $n_queues type veth \
    peer name $veth_name_bridge numrxqueues $n_queues numtxqueues $n_queues

PID=$(sudo docker inspect -f '{{.State.Pid}}' $container_name)
sudo ip link set netns $PID dev $veth_name_container
sudo ovs-vsctl add-port $br_name $veth_name_bridge

sudo nsenter -t $PID -n ip link set $veth_name_container up
sudo ip link set $veth_name_bridge up
sudo nsenter -t $PID -n ip addr add ${veth_container_ip}/24 dev $veth_name_container
sudo ip addr add ${veth_bridge_ip}/24 dev $veth_name_bridge

sudo ethtool -K $veth_name_bridge tx off gso off tso off sg off gro off lro off rx on
sudo nsenter -t $PID -n ethtool -K $veth_name_container tx off gso off tso off sg off gro off lro off rx on