import time
from nodes.node import Node

class BareTas(Node):
  
  def __init__(self, defaults, machine_config, tas_config,
      cset_configs, wmanager, setup_pane_name, cleanup_pane_name):

    Node.__init__(self, defaults, machine_config, cset_configs,
        wmanager, setup_pane_name, cleanup_pane_name)
        
    self.tas_config = tas_config

  def cleanup(self):
    super().cleanup()
    self.remove_file(self.cleanup_pane, self.tas_config.project_dir + "/flexnic_os")