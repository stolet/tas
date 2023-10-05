import time
import threading 

from components.tas import TAS
from components.vm import VM
from nodes.node import Node

class OvsTas(Node):
  
  def __init__(self, defaults, machine_config, tas_configs,
      vm_configs, cset_configs, interface, pci_id, wmanager, 
      setup_pane_name, cleanup_pane_name, tunnel):

    Node.__init__(self, defaults, machine_config, cset_configs,
        wmanager, setup_pane_name, cleanup_pane_name)
  
    self.tunnel = tunnel
    self.interface = interface
    self.pci_id = pci_id
    self.tas_configs = tas_configs
    self.vm_configs = vm_configs
    self.vms = []
    self.tas = []

  def setup(self, is_client=False):
    super().setup()

    if self.tunnel:
      self.setup_with_tunnel(is_client)
    else:
      self.setup_without_tunnel()

  def setup_with_tunnel(self, is_client):
    self.ovs_make_install(self.defaults.original_ovs_path)
    self.start_ovsdpdk(self.machine_config.ovs_pmd_mask,
                       self.vm_configs[0].manager_dir)
    self.ovsbr_add_internal("br-int", self.vm_configs[0].manager_dir)

    if is_client:
      remote_ip = self.defaults.server_ip
      mac = self.defaults.client_mac
    else:
      remote_ip = self.defaults.client_ip
      mac = self.defaults.server_mac

    for vm_config in self.vm_configs:
      self.ovsvhost_add("br-int", 
                        "vhost{}".format(vm_config.id),
                        vm_config.n_queues,
                        vm_config.manager_dir)
  
    self.ovstunnel_add("br-int", "gre1", remote_ip, 
                       self.vm_configs[0].manager_dir, key=1)
    self.ovsbr_add_physical("br-phy", mac, 
                            self.vm_configs[0].manager_dir)
    self.ovsbr_add_port("br-phy", self.interface)
    self.set_dpdk_interface(self.interface, self.pci_id, self.vm_configs[0].n_queues)
    self.add_ip("br-phy", self.machine_config.ip + "/24")
    self.interface_up("br-phy")

  def setup_without_tunnel(self):
    self.ovs_make_install(self.defaults.original_ovs_path)
    self.start_ovsdpdk(self.machine_config.ovs_pmd_mask,
                       self.vm_configs[0].manager_dir)
    self.ovsbr_add("br0", 
                   self.machine_config.ip + "/24",
                   self.machine_config.interface,
                   self.vm_configs[0].manager_dir)
    self.set_dpdk_interface(self.interface, self.pci_id, self.vm_configs[0].n_queues)
    
    for vm_config in self.vm_configs:
      self.ovsvhost_add("br0", 
                        "vhost{}".format(vm_config.id),
                        vm_config.n_queues,
                        vm_config.manager_dir)
    
    self.add_ip("br0", self.machine_config.ip + "/24")
    self.interface_up("br0")
  
  def cleanup(self):
    super().cleanup()
    
    if self.tunnel:
      self.cleanup_with_tunnel()
    else:
      self.cleanup_without_tunnel()

  def cleanup_with_tunnel(self):
    self.ovsbr_del("br-int")
    self.ovsbr_del("br-phy")
    self.stop_ovsdpdk(self.vm_configs[0].manager_dir)

    cmd = "sudo ip addr add {} dev {}".format(self.machine_config.ip + "/24",
                                              self.machine_config.interface)
    self.cleanup_pane.send_keys(cmd)
    time.sleep(1)

    cmd = "sudo ip link set dev {} up".format(self.machine_config.interface)
    self.cleanup_pane.send_keys(cmd)
    time.sleep(1)

    for vm in self.vms:
      vm.shutdown()

  def cleanup_without_tunnel(self):
    self.ovsbr_del("br0")
    self.stop_ovsdpdk(self.vm_configs[0].manager_dir)

    cmd = "sudo ip addr add {} dev {}".format(self.machine_config.ip + "/24",
                                              self.machine_config.interface)
    self.cleanup_pane.send_keys(cmd)
    time.sleep(1)

    cmd = "sudo ip link set dev {} up".format(self.machine_config.interface)
    self.cleanup_pane.send_keys(cmd)
    time.sleep(1)

    for vm in self.vms:
      vm.shutdown()
    
  def start_tas(self):
    for i in range(self.nodenum):
      tas_config = self.tas_configs[i]
      
      tas = TAS(defaults=self.defaults, 
        machine_config=self.machine_config, 
        tas_config=tas_config,
        cset_configs=self.cset_configs,
        vm_config=self.vm_configs[i],
        wmanager=self.wmanager)
      
      self.tas.append(tas)
      tas.run_virt()
      time.sleep(3)

  def start_vm(self, vm, vm_config):
    vm.start()
    vm.enable_hugepages()
    vm.enable_noiommu("1af4 1110")
    vm.disable_offloads(self.defaults.tas_interface)
    vm.init_interface(vm_config.vm_ip, self.defaults.tas_interface)
    vm.dpdk_bind(vm_config.vm_ip, self.defaults.tas_interface,
        self.defaults.pci_id)

  def start_vms(self):
    threads = []
    for vm_config in self.vm_configs:
      vm = VM(self.defaults, self.machine_config, vm_config, self.cset_configs, self.wmanager)
      self.vms.append(vm)
      vm_thread = threading.Thread(target=self.start_vm, args=(vm, vm_config))
      threads.append(vm_thread)
      vm_thread.start()

    for t in threads:
      t.join()
      