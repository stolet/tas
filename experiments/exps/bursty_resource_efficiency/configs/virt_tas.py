from configs.gen_config import Defaults
from configs.gen_config import MachineConfig
from configs.gen_config import TasConfig
from configs.gen_config import VMConfig
from configs.gen_config import HostProxyConfig
from configs.gen_config import GuestProxyConfig
from configs.gen_config import ClientConfig
from configs.gen_config import ServerConfig
from configs.gen_config import CSetConfig

class Config:
    def __init__(self, exp_name, n_cores, open_delays):
        self.exp_name = exp_name
        self.defaults = Defaults()

        # Configure Csets
        self.s_cset_configs = []
        self.c_cset_configs = []
        tas_cset = CSetConfig([1,3,5,7,9], "0-1", "tas")
        self.s_cset_configs.append(tas_cset)
        self.c_cset_configs.append(tas_cset)

        vm0_cset = CSetConfig([11,13,15,17], "0-1", "vm0")
        self.s_cset_configs.append(vm0_cset)
        vm1_cset = CSetConfig([19,21,23,25], "0-1", "vm1")
        self.s_cset_configs.append(vm1_cset)
        vm2_cset = CSetConfig([27,29,31,33], "0-1", "vm2")
        self.s_cset_configs.append(vm2_cset)
        vm3_cset = CSetConfig([35,37,39,41], "0-1", "vm3")
        self.s_cset_configs.append(vm3_cset)
        
        client_cset = CSetConfig([11,13,15,17,19,21,23,25,27,29,31,33,35,37,39,41], "0-1", "client")
        self.c_cset_configs.append(client_cset)


        # Server Machine
        self.sstack = 'virt-tas'
        self.snum = 1
        self.snodenum = 4
        self.s_tas_configs = []
        self.s_vm_configs = []
        self.s_proxyg_configs = []
        self.server_configs = []
        
        self.s_machine_config = MachineConfig(ip=self.defaults.server_ip, 
                interface=self.defaults.server_interface,
                stack=self.sstack,
                is_remote=True,
                is_server=True)
        
        self.s_cset_configs.append(tas_cset)

        tas_config = TasConfig(pane=self.defaults.s_tas_pane,
                machine_config=self.s_machine_config,
                project_dir=self.defaults.default_vtas_dir_bare,
                ip=self.s_machine_config.ip,
                cc="const-rate", cc_const_rate=0,
                n_cores=n_cores, cset="tas")
        tas_config.args = tas_config.args + ' --vm-shm-len=4294967296'
        self.s_tas_configs.append(tas_config)

        self.s_proxyh_config = HostProxyConfig(pane=self.defaults.s_proxyh_pane,
                machine_config=self.s_machine_config, block=1,
                comp_dir=self.defaults.default_vtas_dir_bare)

        for idx in range(self.snodenum):
                vm_config = VMConfig(pane=self.defaults.s_vm_pane,
                        machine_config=self.s_machine_config,
                        tas_dir=self.defaults.default_vtas_dir_bare,
                        tas_dir_virt=self.defaults.default_vtas_dir_virt,
                        idx=idx,
                        n_cores=4,
                        cset="vm{}".format(idx),
                        memory=5)
                self.s_vm_configs.append(vm_config)

                proxyg_config = GuestProxyConfig(pane=self.defaults.s_proxyg_pane,
                        machine_config=self.s_machine_config, block=1,
                        comp_dir=self.defaults.default_vtas_dir_virt)
                self.s_proxyg_configs.append(proxyg_config)

                port = 1230 + idx
                server_config = ServerConfig(pane=self.defaults.s_server_pane,
                        idx=idx, vmid=idx,
                        port=port, ncores=3, max_flows=4096, max_bytes=4096,
                        bench_dir=self.defaults.default_vbenchmark_dir_virt,
                        tas_dir=self.defaults.default_vtas_dir_virt)
                self.server_configs.append(server_config)

        # Client machine
        self.cstack = 'bare-tas'
        self.cnum = 4
        self.cnodenum = 1
        self.c_tas_configs = []
        self.c_vm_configs = []
        self.c_proxyg_configs = []
        self.client_configs = []

        self.c_machine_config = MachineConfig(ip=self.defaults.client_ip, 
                interface=self.defaults.client_interface,
                stack=self.cstack,
                is_remote=False,
                is_server=False)
        
        tas_config = TasConfig(pane=self.defaults.c_tas_pane,
                machine_config=self.c_machine_config,
                project_dir=self.defaults.default_otas_dir_bare,
                ip=self.c_machine_config.ip,
                cc="const-rate", cc_const_rate=0,
                cset="tas",
                n_cores=4)
        self.c_tas_configs.append(tas_config)

        for idx in range(self.cnum):
                port = 1230 + idx
                client_config = ClientConfig(exp_name=exp_name, 
                        pane=self.defaults.c_client_pane,
                        idx=idx, vmid=0, stack=self.cstack,
                        ip=self.s_machine_config.ip, port=port, ncores=3,
                        msize=64, mpending=64, nconns=100,
                        open_delay=open_delays[idx], max_msgs_conn=0, max_pend_conns=1,
                        bench_dir=self.defaults.default_obenchmark_dir_bare,
                        tas_dir=self.defaults.default_otas_dir_bare,
                        cset="client",
                        bursty=True,
                        rate_normal=4250, rate_burst=50000, 
                        burst_length=10, burst_interval=50)
                client_config.hist_file = None
                client_config.hist_msgs_file = None
                client_config.hist_open_file = None
                self.client_configs.append(client_config)