#!/bin/bash

#Start DPDK setup.

#Export $RTE_SDK
export RTE_SDK=/usr/src/dpdk-stable-18.11.2

#Bind device to DPDK
echo Binding devices to DPDK..
sudo ip link set ens4 down

#Enable PMD
sudo modprobe uio_pci_generic
#sudo modprobe igb_uio
#sudo insmod /usr/src/dpdk-stable-18.11.1/build/kmod/igb_uio.ko
#sudo modprobe igb_uio

#Bind NICs to DPDK
sudo $RTE_SDK/usertools/dpdk-devbind.py --bind=uio_pci_generic ens4

#Print outputs of dpdk drivers and ovs-vsctl/ovs-ofctl to confirm.
sudo $RTE_SDK/usertools/dpdk-devbind.py -s

#Print number of Hugepages
grep Huge /proc/meminfo
