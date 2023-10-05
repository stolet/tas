import time

from nodes.container_ovs_dpdk.container_ovs_dpdk import ContainerOVSDPDK
from components.client import Client

class ContainerOVSDPDKClient(ContainerOVSDPDK):
  
  def __init__(self, config, wmanager):

    ContainerOVSDPDK.__init__(self, config.defaults, config.c_cset_configs,
                              config.c_machine_config,
                              config.c_container_configs,
                              wmanager,
                              config.defaults.c_setup_pane,
                              config.defaults.c_cleanup_pane,
                              config.defaults.server_interface,
                              config.defaults.server_interface_pci,
                              )

    self.client_configs = config.client_configs
    self.nodenum = config.cnodenum
    self.cnum = config.cnum
    self.clients = []

  def cleanup(self):
    super().cleanup()

    for config in self.container_configs:
        self.cleanup_pane.send_keys(suppress_history=False, cmd='whoami')
        time.sleep(1)

        captured_pane = self.cleanup_pane.capture_pane()
        user = captured_pane[len(captured_pane) - 2]

        # This means we are in the container, so we don't
        # accidentally exit machine
        if user == 'tas':
            self.cleanup_pane.send_keys(suppress_history=False, cmd='exit')
            time.sleep(3)

        kill_container_cmd = "sudo docker stop {}".format(
            config.name)
        self.cleanup_pane.send_keys(kill_container_cmd)
        time.sleep(10)
        kill_container_cmd = "sudo docker remove {}".format(
            config.name)
        time.sleep(1)
        prune_container_cmd = "sudo docker container prune -f"
        self.cleanup_pane.send_keys(prune_container_cmd)
        time.sleep(2)

    for container in self.containers:
        container.shutdown()

  def start_clients(self):
    for i in range(self.nodenum):
      container_config = self.container_configs[i]
      for j in range(self.cnum):
        cidx = self.cnum * i + j
        client_config = self.client_configs[cidx]
        client = Client(self.defaults, 
            self.machine_config,
            client_config, 
            container_config,
            self.cset_configs,
            self.wmanager)
        self.clients.append(client)
        client.run_virt(False, False)
        time.sleep(3)

  def run(self):
    self.setup()
    self.start_containers()
    self.setup_post(is_client=True)
    self.start_clients()

  def save_logs(self, exp_path):
    for client in self.clients:
      client.save_log_virt(exp_path)