import time

from nodes.container_ovs_dpdk.container_ovs_dpdk import ContainerOVSDPDK
from components.server import Server


class ContainerOVSDPDKServer(ContainerOVSDPDK):

    def __init__(self, config, wmanager):

        ContainerOVSDPDK.__init__(self, config.defaults, config.s_cset_configs, 
                                  config.s_machine_config,
                                  config.s_container_configs,
                                  wmanager,
                                  config.defaults.s_setup_pane,
                                  config.defaults.s_cleanup_pane,
                                  config.defaults.server_interface,
                                  config.defaults.server_interface_pci,
                                  )

        self.server_configs = config.server_configs
        self.nodenum = config.snodenum
        self.snum = config.snum

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
    

    def start_servers(self):
        for i in range(self.nodenum):
            container_config = self.container_configs[i]
            for j in range(self.snum):
                sidx = self.snum * i + j
                server_config = self.server_configs[sidx]
                server = Server(self.defaults,
                                self.machine_config,
                                server_config,
                                container_config,
                                self.cset_configs,
                                self.wmanager)
                server.run_virt(False, False)
                time.sleep(3)

    def run(self):
        self.setup()
        self.start_containers()
        self.setup_post(is_client=False)
        self.start_servers()