import time

from components.tas import TAS
from components.vm import VM
from nodes.node import Node

class OvsTas(Node):
  
  def __init__(self, defaults, machine_config, tas_configs,
      vm_configs, wmanager, 
      setup_pane_name, cleanup_pane_name):

    Node.__init__(self, defaults, machine_config, wmanager, 
        setup_pane_name, cleanup_pane_name)
        
    self.tas_configs = tas_configs
    self.vm_configs = vm_configs
    self.vms = []
    self.tas = []

  def setup(self):
    super().setup()

    self.start_ovs(self.defaults.ovs_ctl_path)
    self.ovsbr_add("br0", 
                   self.machine_config.ip + "/24", 
                   self.machine_config.interface,
                   self.vm_configs[0].manager_dir)
    
    for vm_config in self.vm_configs:
      # Tap that allows us to ssh to VM
      self.ovstap_add("br0", 
                      "tap{}".format(vm_config.id), 
                      vm_config.manager_dir)
      # TAP used by OvS
      self.ovstap_add("br0", 
                      "ovstap{}".format(vm_config.id), 
                      vm_config.manager_dir)

  def cleanup(self):
    super().cleanup()
    self.ovsbr_del("br0")
    self.stop_ovs(self.defaults.ovs_ctl_path)

    cmd = "sudo ip addr add {} dev {}".format(self.machine_config.ip + "/24",
                                              self.machine_config.interface)
    self.cleanup_pane.send_keys(cmd)
    time.sleep(1)

    cmd = "sudo ip link set dev {} up".format(self.machine_config.interface)
    self.cleanup_pane.send_keys(cmd)
    time.sleep(1)

    for vm_config in self.vm_configs:
      self.tap_down("tap{}".format(vm_config.id), vm_config.manager_dir)
      self.tap_down("ovstap{}".format(vm_config.id), vm_config.manager_dir)
    
  def start_tas(self):
    for i in range(self.nodenum):
      tas_config = self.tas_configs[i]
      
      tas = TAS(defaults=self.defaults, 
        machine_config=self.machine_config, 
        tas_config=tas_config, 
        vm_config=self.vm_configs[i],
        wmanager=self.wmanager)
      
      self.tas.append(tas)
      tas.run_virt()
      time.sleep(3)

  def start_vms(self):
    for vm_config in self.vm_configs:
      vm = VM(self.defaults, self.machine_config, vm_config, self.wmanager)
      self.vms.append(vm)
      vm.start()
      vm.enable_hugepages()
      vm.enable_noiommu("1af4 1110")
      vm.init_interface(vm_config.vm_ip, self.defaults.vm_interface)
      vm.init_interface(vm_config.tas_tap_ip, self.defaults.tas_interface)
      vm.dpdk_bind(vm_config.tas_tap_ip, self.defaults.tas_interface,
          self.defaults.pci_id)