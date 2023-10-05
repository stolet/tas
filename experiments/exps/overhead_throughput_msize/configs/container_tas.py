from configs.gen_config import Defaults
from configs.gen_config import MachineConfig
from configs.gen_config import TasConfig
from configs.gen_config import ContainerConfig
from configs.gen_config import ClientConfig
from configs.gen_config import ServerConfig
from configs.gen_config import CSetConfig

class Config:
    def __init__(self, exp_name, msize):
        self.exp_name = exp_name
        self.defaults = Defaults()

        # Configure csets
        self.s_cset_configs = []
        self.c_cset_configs = []
        tas_cset = CSetConfig([1,3,5,7,9,11], "0-1", "tas_server")
        # self.s_cset_configs.append(tas_cset)
        tas_cset = CSetConfig([1,3,5,7,9,11], "0-1", "tas_client")
        # self.c_cset_configs.append(tas_cset)

        container0_cset = CSetConfig([13,15,17,19,21,23,25,27,29,31,33,35,37,39,41,43], "0-1", "container0_server")
        # self.s_cset_configs.append(container0_cset)

        container0_cset = CSetConfig([13,15,17,19,21,23,25,27,29,31,33,35,37,39,41,43], "0-1", "container0_client")
        # self.c_cset_configs.append(container0_cset)

        # Server Machine
        self.sstack = 'container-tas'
        self.snum = 1
        self.snodenum = 1
        self.s_container_configs = []
        self.s_tas_configs = []
        self.server_configs = []

        self.s_machine_config = MachineConfig(ip=self.defaults.server_ip,
                                              interface=self.defaults.server_interface,
                                              stack=self.sstack,
                                              ovs_pmd_mask="0x555",
                                              is_remote=True,
                                              is_server=True)

        container0_config = ContainerConfig(pane=self.defaults.s_container_pane,
                                            machine_config=self.s_machine_config,
                                            vtas_dir=self.defaults.default_vtas_dir_bare,
                                            vtas_dir_virt=self.defaults.default_vtas_dir_virt,
                                            tas_dir=self.defaults.default_otas_dir_bare,
                                            idx=0,
                                            n_cores=12,
                                            memory=10,
                                            n_queues=10,
                                            cset=None)
        tas_config = TasConfig(pane=self.defaults.s_tas_pane,
                               machine_config=self.s_machine_config,
                               project_dir=self.defaults.default_otas_dir_bare,
                               ip=self.s_machine_config.ip,
                               cset=None,
                               n_cores=5)

        self.s_container_configs.append(container0_config)
        self.s_tas_configs.append(tas_config)

        server0_config = ServerConfig(pane=self.defaults.s_server_pane,
                                      idx=0, vmid=0,
                                      port=1234, ncores=12, max_flows=4096, max_bytes=4096,
                                      cset=None,
                                      bench_dir=self.defaults.default_obenchmark_dir_virt,
                                      tas_dir=self.defaults.default_otas_dir_virt)
        self.server_configs.append(server0_config)

        # Client Machine
        self.cstack = 'container-tas'
        self.cnum = 1
        self.cnodenum = 1
        self.c_container_configs = []
        self.c_tas_configs = []
        self.client_configs = []

        self.c_machine_config = MachineConfig(ip=self.defaults.client_ip,
                                              interface=self.defaults.client_interface,
                                              stack=self.cstack,
                                              ovs_pmd_mask="0x555",
                                              is_remote=False,
                                              is_server=False)

        container0_config = ContainerConfig(pane=self.defaults.c_container_pane,
                                            machine_config=self.c_machine_config,
                                            vtas_dir=self.defaults.default_vtas_dir_bare,
                                            vtas_dir_virt=self.defaults.default_vtas_dir_virt,
                                            tas_dir=self.defaults.default_otas_dir_bare,
                                            idx=0,
                                            n_cores=16,
                                            memory=10,
                                            n_queues=12,
                                            cset=None)
        tas_config = TasConfig(pane=self.defaults.c_tas_pane,
                machine_config=self.c_machine_config,
                project_dir=self.defaults.default_otas_dir_bare,
                ip=self.c_machine_config.ip,
                cset=None,
                n_cores=5)

        self.c_container_configs.append(container0_config)
        self.c_tas_configs.append(tas_config)

        client0_config = ClientConfig(exp_name=exp_name,
                                      pane=self.defaults.c_client_pane,
                                      idx=0, vmid=0, stack=self.cstack,
                                      ip=self.defaults.server_ip, port=1234, ncores=12,
                                      msize=msize, mpending=64, nconns=100,
                                      open_delay=10, max_msgs_conn=0, max_pend_conns=1,
                                      cset=None,
                                      bench_dir=self.defaults.default_obenchmark_dir_virt,
                                      tas_dir=self.defaults.default_otas_dir_virt)
        client0_config.hist_out = None
        client0_config.hist_file = None
        client0_config.hist_msgs_file = None
        client0_config.hist_open_file = None
        client0_config.hist_msgs_out = None
        client0_config.hist_open_out = None
        self.client_configs.append(client0_config)