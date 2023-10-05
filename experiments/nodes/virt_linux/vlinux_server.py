import time

from nodes.virt_linux.vlinux import VirtLinux
from components.server import Server

class VirtLinuxServer(VirtLinux):
  
  def __init__(self, config, wmanager):

    VirtLinux.__init__(self, config.defaults, config.s_machine_config,
        config.s_vm_configs, wmanager, 
        config.defaults.s_setup_pane, 
        config.defaults.s_cleanup_pane)

    self.server_configs = config.server_configs
    self.nodenum = config.snodenum
    self.snum = config.snum

  def start_servers(self):
    for i in range(self.nodenum):
      vm_config = self.vm_configs[i]
      for j in range(self.snum):
        sidx = self.snum * i + j
        server_config = self.server_configs[sidx]
        server = Server(self.defaults, 
                self.machine_config,
                server_config, 
                vm_config,
                self.cset_configs,
                self.wmanager)
        server.run_virt(False, False)
        time.sleep(3)

  def run(self):
    self.setup()
    self.start_vms()
    self.start_servers()
