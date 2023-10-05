import time
import threading

from nodes.vtas_bare.vtasbare import VTasBare
from components.tas import TAS
from components.client import Client

class VTasBareClient(VTasBare):
  
  def __init__(self, config, wmanager):

    VTasBare.__init__(self, config.defaults, config.c_machine_config,
        config.c_tas_configs[0], config.c_cset_configs, wmanager, 
        config.defaults.c_setup_pane, config.defaults.c_cleanup_pane)

    self.client_configs = config.client_configs
    self.nodenum = config.cnodenum
    self.cnum = config.cnum
    self.clients = []

  def start_client(self, client_config):
      client = Client(self.defaults, 
          self.machine_config,
          client_config, 
          None,
          self.cset_configs,
          self.wmanager)
      client.pane.send_keys("export TAS_GROUP={}".format(client_config.groupid))
      self.clients.append(client)
      client.run_bare(True, True)
      time.sleep(3)

  def start_clients(self):
    threads = []
    for client_config in self.client_configs:
        client_thread = threading.Thread(target=self.start_client, 
                                         args=(client_config,))
        threads.append(client_thread)
        client_thread.start()
    
    for t in threads:
      t.join()


  def run(self):
    self.setup()
    self.tas = TAS(self.defaults, self.machine_config, 
        self.tas_config, self.cset_configs, self.wmanager)
    
    self.tas.run_bare()
    time.sleep(5)
    self.start_clients()

  def save_logs(self, exp_path):
    for client in self.clients:
      client.save_log_bare(exp_path)

    self.tas.save_log_bare(exp_path)
