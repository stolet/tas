import time

from nodes.container_virtuoso.container_virtuoso import ContainerVirtuoso
from components.client import Client


class ContainerVirtuosoClient(ContainerVirtuoso):

    def __init__(self, config, wmanager):

        ContainerVirtuoso.__init__(self, config.defaults, 
                                   config.c_cset_configs,
                                   config.c_machine_config,
                                   config.c_container_configs,
                                   config.c_tas_configs[0],
                                   wmanager,
                                   config.defaults.c_setup_pane,
                                   config.defaults.c_cleanup_pane,
                                   config.defaults.client_interface,
                                   config.defaults.client_interface_pci,
                                   config.tunnel,
                                   )

        self.client_configs = config.client_configs
        self.nodenum = config.cnodenum
        self.cnum = config.cnum
        self.clients = []
        self.server_container_configs = config.s_container_configs

    def setup_tunnels(self):
        self.ovs_make_install(self.defaults.modified_ovs_path)
        self.start_ovs(self.script_dir)
        self.ovsbr_add_vtuoso("br0", self.script_dir)

        for i, container_config in enumerate(self.container_configs):
            rxport_name = "rx_vtuoso{}".format(container_config.id)
            txport_name = "tx_vtuoso{}".format(container_config.id)
            self.ovsport_add_vtuoso("br0", rxport_name, "virtuosorx", 
                                    container_config.id, 
                                    container_config.manager_dir)
            self.ovsport_add_vtuoso("br0", txport_name, "virtuosotx",
                                    container_config.id, 
                                    container_config.manager_dir,
                                    out_remote_ip=self.defaults.server_ip, 
                                    out_local_ip=self.defaults.client_ip,
                                    in_remote_ip=self.server_container_configs[i].veth_container_ip, 
                                    in_local_ip=container_config.veth_container_ip,
                                    key=i + 1)
            self.ovsflow_add("br0", rxport_name, txport_name, container_config.manager_dir)


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
                client.pane.send_keys(
                    "export TAS_GROUP={}".format(client_config.groupid))
                self.clients.append(client)
                client.run_virt(True, True)
                client.pane.send_keys("tas")
                time.sleep(3)

    def run(self):
        self.setup()
        self.start_tas()
        if self.tunnel:
            self.setup_tunnels()
        self.start_containers()
        self.start_clients()

    def save_logs(self, exp_path):
        for client in self.clients:
            client.save_log_virt(exp_path)