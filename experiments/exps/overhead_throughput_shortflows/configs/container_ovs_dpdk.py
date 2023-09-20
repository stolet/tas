from configs.gen_config import Defaults
from configs.gen_config import MachineConfig
from configs.gen_config import ContainerConfig
from configs.gen_config import ClientConfig
from configs.gen_config import ServerConfig
from configs.gen_config import CSetConfig


class Config:
    def __init__(self, exp_name, flow_len):
        msize = 64
        self.exp_name = exp_name
        self.defaults = Defaults()

        # Configure csets
        self.s_cset_configs = []
        self.c_cset_configs = []

        container0_cset = CSetConfig(
            [i for i in range(1,26,2)], "0-1", "container0_server")
        self.s_cset_configs.append(container0_cset)

        container0_cset = CSetConfig(
            [i for i in range(1,26,2)], "0-1", "container0_client")
        self.c_cset_configs.append(container0_cset)

        # Server Machine
        self.sstack = 'container-ovsdpdk'
        self.snum = 1
        self.snodenum = 1
        self.s_container_configs = []
        self.server_configs = []

        self.s_machine_config = MachineConfig(ip=self.defaults.server_ip,
                                              interface=self.defaults.server_interface,
                                              stack=self.sstack,
                                              is_remote=True,
                                              is_server=True,
                                              ovs_pmd_mask="0x1551")

        container0_config = ContainerConfig(pane=self.defaults.s_container_pane,
                                            machine_config=self.s_machine_config,
                                            vtas_dir=self.defaults.default_vtas_dir_bare,
                                            vtas_dir_virt=self.defaults.default_vtas_dir_virt,
                                            tas_dir=self.defaults.default_vtas_dir_bare,
                                            idx=0,
                                            n_cores=6,
                                            memory=5,
                                            n_queues=10,
                                            cset="container0_server")

        self.s_container_configs.append(container0_config)

        server0_config = ServerConfig(pane=self.defaults.s_server_pane,
                                      idx=0, vmid=0,
                                      port=1234, ncores=5, max_flows=4096, max_bytes=msize,
                                      bench_dir=self.defaults.default_obenchmark_dir_virt,
                                      tas_dir=self.defaults.default_otas_dir_virt)
        self.server_configs.append(server0_config)

        # Client Machine
        self.cstack = 'container-ovsdpdk'
        self.cnum = 1
        self.cnodenum = 1
        self.c_container_configs = []
        self.client_configs = []

        self.c_machine_config = MachineConfig(ip=self.defaults.client_ip,
                                              interface=self.defaults.client_interface,
                                              stack=self.cstack,
                                              is_remote=False,
                                              is_server=False,
                                              ovs_pmd_mask="0x1551")

        container0_config = ContainerConfig(pane=self.defaults.c_container_pane,
                                            machine_config=self.c_machine_config,
                                            vtas_dir=self.defaults.default_vtas_dir_bare,
                                            vtas_dir_virt=self.defaults.default_vtas_dir_virt,
                                            tas_dir=self.defaults.default_vtas_dir_bare,
                                            idx=0,
                                            n_cores=6,
                                            memory=5,
                                            n_queues=10,
                                            cset="container0_client")

        self.c_container_configs.append(container0_config)

        client0_config = ClientConfig(exp_name=exp_name,
                                      pane=self.defaults.c_client_pane,
                                      idx=0, vmid=0, stack=self.cstack,
                                      ip=self.s_container_configs[0].veth_container_ip, port=1234, ncores=5,
                                      msize=msize, mpending=1, nconns=100,
                                      open_delay=0, max_msgs_conn=flow_len, max_pend_conns=16,
                                      bench_dir=self.defaults.default_obenchmark_dir_virt,
                                      tas_dir=self.defaults.default_otas_dir_virt)
        client0_config.hist_out = None
        client0_config.hist_file = None
        client0_config.hist_msgs_file = None
        client0_config.hist_open_file = None
        client0_config.hist_msgs_out = None
        client0_config.hist_open_out = None
        self.client_configs.append(client0_config)