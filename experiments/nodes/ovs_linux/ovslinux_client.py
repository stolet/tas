import time
import threading

from nodes.ovs_linux.ovslinux import OvsLinux
from components.client import Client

class OvsLinuxClient(OvsLinux):
  
  def __init__(self, config, wmanager):

    if hasattr(config, 'c_tunnel'):
      tunnel = config.c_tunnel
    else:
      tunnel = False

    OvsLinux.__init__(self, config.defaults, config.c_machine_config,
        config.c_vm_configs, config.c_cset_configs,
        config.defaults.client_interface,
        config.defaults.client_interface_pci,
        wmanager, 
        config.defaults.c_setup_pane, 
        config.defaults.c_cleanup_pane, 
        tunnel)

    self.client_configs = config.client_configs
    self.nodenum = config.cnodenum
    self.cnum = config.cnum
    self.clients = []

  def cleanup(self):
    super().cleanup()

  def start_client(self, cidx, vm_config):
    client_config = self.client_configs[cidx]
    client = Client(self.defaults, 
        self.machine_config,
        client_config, 
        vm_config, 
        self.cset_configs,
        self.wmanager)
    self.clients.append(client)
    client.run_virt(True, False)
    time.sleep(3)

  def start_clients(self):
    threads = []
    for i in range(self.nodenum):
      vm_config = self.vm_configs[i]
      for j in range(self.cnum):
        cidx = self.cnum * i + j
        client_thread = threading.Thread(target=self.start_client, 
                                         args=(cidx, vm_config,))
        threads.append(client_thread)
        client_thread.start()
    
    for t in threads:
      t.join()


  def run(self):
    self.setup(is_client=True)
    self.start_vms()
    self.start_clients()

  def save_logs(self, exp_path):
    for client in self.clients:
      client.save_log_virt(exp_path)
