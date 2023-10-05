#!/usr/bin/env bash
set -ex

stack=$1
container_id=$2
n_cores=$3
memory=$4 # In Gigabytes
container_name=$5
tas_dir=$6
cset=$7

image_name="virtuoso"
otas_interpose_path_virt="/home/tas/libtas_interpose.so"

if [[ "$stack" == 'container-ovsdpdk' ]]; then
    docker -- run --net=none --name $container_name \
    --cpuset-cpus 1,3,5,7,9,11,13,15,17,19,21,23,25,27,29,31,33,35,37,39,41,43 \
    --memory=${memory}g \
    --cpus=${n_cores} \
    -d $image_name sleep infinity;
elif [[ "$stack" == 'container-tas' ]]; then
    docker -- run --net=none --name $container_name \
    -v ${tas_dir}/flexnic_os:/home/tas/projects/o-tas/tas/flexnic_os \
    --cpuset-cpus 13,15,17,19,21,23,25,27,29,31,33,35,37,39,41,43 \
    -v /dev/hugepages:/dev/hugepages \
    -v /dev/shm:/dev/shm \
    --memory=${memory}g \
    --cpus=${n_cores} \
    -d $image_name sleep infinity;
elif [[ "$stack" == 'container-virtuoso' ]]; 
then
    sudo docker -- run --net=none --name $container_name \
    -v ${tas_dir}/flexnic_os_vm_${container_id}:/home/tas/projects/tas/flexnic_os_vm_${container_id} \
    --cpuset-cpus 13,15,17,19,21,23,25,27,29,31,33,35,37,39,41,43 \
    -v /dev/hugepages/tas_memory_vm${container_id}:/dev/hugepages/tas_memory_vm${container_id} \
    -v /dev/shm/tas_info:/dev/shm/tas_info \
    --memory=${memory}g \
    --cpus=${n_cores} \
    -d $image_name sleep infinity;
fi