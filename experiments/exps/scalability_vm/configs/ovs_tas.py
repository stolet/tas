from exps.scalability_vm.helpers import *
from configs.gen_config import Defaults
from configs.gen_config import MachineConfig
from configs.gen_config import TasConfig
from configs.gen_config import VMConfig
from configs.gen_config import ClientConfig
from configs.gen_config import ServerConfig

class Config:
    def __init__(self, exp_name, n_vms):
        self.exp_name = exp_name
        self.defaults = Defaults()
        vm_cores = 2
        
        # Configure Csets
        pmd_mask = "0x555"
        ovs_cores = [2,4,6,8,10,12]

        self.s_cset_configs = []
        self.c_cset_configs = []

        vm_cset = create_vm_csets(n_vms, vm_cores, skip_cores=ovs_cores, exclusive=False)
        self.c_cset_configs = self.c_cset_configs + vm_cset
        self.s_cset_configs = self.s_cset_configs + vm_cset

        # Server Machine
        self.sstack = 'ovs-tas'
        self.snum = 1
        self.snodenum = n_vms
        self.s_tas_configs = []
        self.s_vm_configs = []
        self.s_proxyg_configs = []
        self.server_configs = []
        
        self.s_machine_config = MachineConfig(ip=self.defaults.server_ip, 
                interface=self.defaults.server_interface,
                stack=self.sstack,
                ovs_pmd_mask=pmd_mask,
                is_remote=True,
                is_server=True)
        
        for idx in range(self.snodenum):
                vm_config = VMConfig(pane=self.defaults.s_vm_pane,
                        machine_config=self.s_machine_config,
                        tas_dir=self.defaults.default_vtas_dir_bare,
                        tas_dir_virt=self.defaults.default_vtas_dir_virt,
                        idx=idx,
                        n_cores=vm_cores,
                        cset="vm{}".format(idx),
                        memory=3,
                        n_queues=1)
                tas_config = TasConfig(pane=self.defaults.s_tas_pane,
                        machine_config=self.s_machine_config,
                        project_dir=self.defaults.default_otas_dir_virt,
                        ip=vm_config.vm_ip,
                        n_cores=1, pci="00:03.0")
                tas_config.args = tas_config.args + " --fp-no-rss --fp-no-xsumoffload --shm-len=536870912"

                self.s_tas_configs.append(tas_config)
                self.s_vm_configs.append(vm_config)

                port = 1230 + idx
                server0_config = ServerConfig(pane=self.defaults.s_server_pane,
                        idx=idx, vmid=idx,
                        port=port, ncores=1, max_flows=4096, max_bytes=4096,
                        bench_dir=self.defaults.default_obenchmark_dir_virt,
                        tas_dir=self.defaults.default_otas_dir_virt)
                self.server_configs.append(server0_config)



        self.cstack = 'ovs-tas'
        self.cnum = 1
        self.cnodenum = n_vms
        self.c_tas_configs = []
        self.c_vm_configs = []
        self.c_proxyg_configs = []
        self.client_configs = []

        self.c_machine_config = MachineConfig(ip=self.defaults.client_ip, 
                interface=self.defaults.client_interface,
                stack=self.cstack,
                ovs_pmd_mask=pmd_mask,
                is_remote=False,
                is_server=False)

        for idx in range(self.cnodenum):
                vm_config = VMConfig(pane=self.defaults.c_vm_pane,
                        machine_config=self.c_machine_config,
                        tas_dir=self.defaults.default_vtas_dir_bare,
                        tas_dir_virt=self.defaults.default_vtas_dir_virt,
                        idx=idx,
                        n_cores=vm_cores,
                        cset="vm{}".format(idx),
                        memory=3,
                        n_queues=1)
                tas_config = TasConfig(pane=self.defaults.c_tas_pane,
                        machine_config=self.c_machine_config,
                        project_dir=self.defaults.default_otas_dir_virt,
                        ip=vm_config.vm_ip,
                        n_cores=1, pci="00:03.0")
                tas_config.args = tas_config.args + " --fp-no-rss --fp-no-xsumoffload --shm-len=536870912"

                self.c_tas_configs.append(tas_config)
                self.c_vm_configs.append(vm_config)

                port = 1230 + idx
                client0_config = ClientConfig(exp_name=exp_name, 
                        pane=self.defaults.c_client_pane,
                        idx=0, vmid=idx, stack=self.cstack,
                        ip=self.s_vm_configs[idx].vm_ip, port=port, ncores=1,
                        msize=64, mpending=64, nconns=100,
                        open_delay=10, max_msgs_conn=0, max_pend_conns=1,
                        bench_dir=self.defaults.default_obenchmark_dir_virt,
                        tas_dir=self.defaults.default_otas_dir_virt)
                self.client_configs.append(client0_config)