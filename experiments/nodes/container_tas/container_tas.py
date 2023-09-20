import time
import threading

from components.tas import TAS
from components.container import Container
from nodes.node import Node


class ContainerTas(Node):
    def __init__(
        self,
        defaults,
        cset_configs,
        machine_config,
        container_configs,
        tas_config,
        wmanager,
        setup_pane_name,
        cleanup_pane_name,
        interface,
        pci_id,
    ):
        Node.__init__(
            self, defaults, machine_config, cset_configs, wmanager, setup_pane_name, cleanup_pane_name
        )

        self.container_configs = container_configs
        self.containers = []
        self.tas_config = tas_config
        self.tas = None
        self.interface = interface
        self.pci_id = pci_id
        self.script_dir = container_configs[0].manager_dir

    def cleanup(self):
        super().cleanup()
        if self.tas:
            self.tas.cleanup(self.cleanup_pane)
        else:
            remove_tas_socket_com = "find {} -name \"*flexnic_os*\" | xargs rm -r".format(self.tas_config.project_dir)
            self.cleanup_pane.send_keys(remove_tas_socket_com)

        for container in self.containers:
            container.shutdown()
    
    def start_tas(self):
        self.tas = TAS(defaults=self.defaults,
                    machine_config=self.machine_config,
                    tas_config=self.tas_config,
                    wmanager=self.wmanager)
        self.tas.run_bare()
        time.sleep(3)

    def start_containers(self):
        threads = []
        for container_config in self.container_configs:
            container = Container(
                self.defaults,
                self.machine_config,
                container_config,
                self.wmanager
            )
            self.containers.append(container)
            container_thread = threading.Thread(target=container.start)
            threads.append(container_thread)
            container_thread.start()

        for t in threads:
            t.join()