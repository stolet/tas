import time

from nodes.container_virtuoso.container_virtuoso import ContainerVirtuoso
from components.server import Server


class ContainerVirtuosoServer(ContainerVirtuoso):

    def __init__(self, config, wmanager):

        ContainerVirtuoso.__init__(self, config.defaults,
                                   config.s_cset_configs,
                                   config.s_machine_config,
                                   config.s_container_configs,
                                   config.s_tas_configs[0],
                                   wmanager,
                                   config.defaults.s_setup_pane,
                                   config.defaults.s_cleanup_pane,
                                   config.defaults.server_interface,
                                   config.defaults.server_interface_pci,
                                   config.tunnel,
                                   )

        self.server_configs = config.server_configs
        self.nodenum = config.snodenum
        self.snum = config.snum
        self.client_container_configs = config.c_container_configs

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
                                    out_remote_ip=self.defaults.client_ip, 
                                    out_local_ip=self.defaults.server_ip,
                                    in_remote_ip=self.client_container_configs[i].veth_container_ip, 
                                    in_local_ip=container_config.veth_container_ip,
                                    key=i + 1)
            self.ovsflow_add("br0", rxport_name, txport_name, container_config.manager_dir)

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
                server.pane.send_keys(
                    "export TAS_GROUP={}".format(server_config.groupid))
                server.run_virt(True, True)
                server.pane.send_keys("tas")
                time.sleep(3)

    def run(self):
        self.setup()
        self.start_tas()
        if self.tunnel:
            self.setup_tunnels()
        self.start_containers()
        self.start_servers()