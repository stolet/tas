""" IP tables may drop packets on the bridge so run the following
    on the host machine if that happens
    echo 0 > /proc/sys/net/bridge/bridge-nf-call-iptables """

import numpy as np

class Defaults:
    def __init__(self):
        self.client_ip = '192.168.10.13'
        self.server_ip = '192.168.10.14'

        self.pane_prefix = 'e_'
        self.server_pane_prefix = '{}server'.format(self.pane_prefix)
        self.client_pane_prefix = '{}client'.format(self.pane_prefix)

        # Pane names
        self.s_tas_pane = "{}_tas".format(self.server_pane_prefix)
        self.s_vm_pane = "{}_vm".format(self.server_pane_prefix)
        self.s_proxyg_pane = "{}_proxyg".format(self.server_pane_prefix)
        self.s_proxyh_pane = "{}_proxy_h".format(self.server_pane_prefix)
        self.s_server_pane = "{}".format(self.server_pane_prefix)
        self.s_container_pane = "{}_container".format(self.server_pane_prefix)
        self.s_savelogs_pane = "{}_savelogs".format(self.server_pane_prefix)
        self.s_setup_pane = "{}_setup".format(self.server_pane_prefix)
        self.s_cleanup_pane = "{}_cleanup".format(self.server_pane_prefix)

        self.c_tas_pane = "{}_tas".format(self.client_pane_prefix)
        self.c_vm_pane = "{}_vm".format(self.client_pane_prefix)
        self.c_proxyg_pane = "{}_proxyg".format(self.client_pane_prefix)
        self.c_proxyh_pane = "{}_proxyh".format(self.client_pane_prefix)
        self.c_client_pane = "{}".format(self.client_pane_prefix)
        self.c_container_pane = "{}_container".format(self.client_pane_prefix)
        self.c_savelogs_pane = "{}_savelogs".format(self.client_pane_prefix)
        self.c_setup_pane = "{}_setup".format(self.client_pane_prefix)
        self.c_cleanup_pane = "{}_cleanup".format(self.client_pane_prefix)

        # Mellanox interfaces on client and server machine
        self.client_interface = 'enp216s0f0np0'
        self.client_interface_pci = "0000:d8:00.0"
        self.client_mac = "b8:59:9f:c4:af:e6"
        self.server_interface = 'enp216s0f0np0'
        self.server_interface_pci = "0000:d8:00.0"
        self.server_mac = "b8:59:9f:c4:af:66"

        ### INTERNAL VM CONFIGS ###
        # Network interface used to set ip for a VM
        self.vm_interface = "enp0s3"
        # Network interface used to bind TAS in tap VM
        self.tas_interface = "enp0s3"
        # PCI Id of TAS interface inside a VM
        self.pci_id = "0000:00:03.0"
        ############################

        # All cores in client machine
        self.c_cores = [0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,
                        18,19,20,21,22,23,24,25,26,27,28,29,30,31,
                        32,33,34,35,36,37,38,39,40,41,42,42]
        # Cores in socket 0
        self.c_cores_s0 = [0,2,4,6,8,10,12,14,16,18,20,22,24,26,28,
                           30,32,34,36,38,40,42]
        # Cores in socket 1
        self.c_cores_s1 = [1,3,5,7,9,11,13,15,17,19,21,23,25,27,29,
                           31,33,35,37,39,41,43]
        # All cores in server machine
        self.s_cores = [0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,
                        18,19,20,21,22,23,24,25,26,27,28,29,30,31,
                        32,33,34,35,36,37,38,39,40,41,42,42]
        # Cores in socket 0
        self.s_cores_s0 = [0,2,4,6,8,10,12,14,16,18,20,22,24,26,28,
                           30,32,34,36,38,40,42]
        # Cores in socket 1
        self.s_cores_s1 = [1,3,5,7,9,11,13,15,17,19,21,23,25,27,29,
                           31,33,35,37,39,41,43]
        

        self.remote_connect_cmd = 'ssh swsnetlab04'

        self.home_dir = '/local/mstolet'
        self.home_dir_virt = '/home/tas'

        self.default_vtas_dir_bare = '{}/projects/tas'.format(self.home_dir)
        self.default_vtas_dir_virt = '{}/projects/tas'.format(self.home_dir_virt)
        self.default_otas_dir_bare = '{}/projects/o-tas/tas'.format(self.home_dir)
        self.default_otas_dir_virt = '{}/projects/o-tas/tas'.format(self.home_dir_virt)

        self.default_vbenchmark_dir_bare = '{}/projects/benchmarks'.format(self.home_dir)
        self.default_vbenchmark_dir_virt = '{}/projects/benchmarks'.format(self.home_dir_virt)
        self.default_obenchmark_dir_bare = '{}/projects/o-benchmarks/benchmarks'.format(self.home_dir)
        self.default_obenchmark_dir_virt = '{}/projects/o-benchmarks/benchmarks'.format(self.home_dir_virt)

        self.ovs_ctl_path = "/usr/local/share/openvswitch/scripts/ovs-ctl"
        self.original_ovs_path = "/local/mstolet/projects/o-ovs/ovs"
        self.modified_ovs_path = "/local/mstolet/projects/ovs"

class MachineConfig:
    def __init__(self, ip, interface, stack, is_remote, is_server,
                 ovs_pmd_mask="0x55555"):
        self.is_server = is_server
        self.is_remote = is_remote
        self.interface = interface
        self.ip = ip
        self.stack = stack
        self.ovs_pmd_mask = ovs_pmd_mask

class CSetConfig:
    def __init__(self, cores, mem, name, exclusive=False):
        self.cores = cores
        self.mem = mem
        self.name = name
        self.exclusive = exclusive
        self.cores_arg = "{}".format(cores[0])
        if len(cores) > 1:
            for i in range(1, len(cores)):
                self.cores_arg = "{},{}".format(self.cores_arg, cores[i])

class TasConfig:
    def __init__(self, pane, machine_config, project_dir, ip, n_cores, 
            pci="d8:00.0",
            cset="tas",
            cores=[1,3,5,7,9,11,13,15,17,19,21],
            cc="timely", 
            cc_timely_min_rtt="10",
            cc_timely_tlow="30", cc_timely_thigh="1000",
            cc_timely_minrate="10000", cc_timely_step="10000",
            cc_const_rate=0):
        self.name = "server" if machine_config. is_server else "client"
        
        self.project_dir = project_dir
        
        self.out_dir = self.project_dir + '/out'
        self.out_file = ''
        if machine_config.is_server:
            self.out_file = 'tas_s'
        else:
            self.out_file = 'tas_c'
        self.out = self.out_dir + '/' + self.out_file
        
        self.comp_dir = self.project_dir
        self.comp_cmd = 'make -j6'
        self.clean_cmd = 'make clean'
        self.lib_so = self.comp_dir + 'lib/libtas_interpose.so'
        self.exec_file = self.comp_dir + '/tas/tas'
        self.args = '--ip-addr={}/24 --fp-cores-max={}'.format(ip, n_cores) + \
            ' --fp-no-autoscale --fp-no-ints' + \
            ' --cc={}'.format(cc) + \
            ' --dpdk-extra="-a{}"'.format(pci)   
        
        self.cset = cset
        self.cores = np.array(cores)[range(0, n_cores + 1)]
        self.lcores = '--lcores='
        for i, core in enumerate(self.cores):
            if i == len(self.cores) - 1:
                self.lcores += "{}@{}".format(i, core)
            else:
                self.lcores += "{}@{},".format(i, core)

        if cc == "const-rate":
            self.args = self.args + " --cc-const-rate={}".format(cc_const_rate)

        if cc == "timely":
            self.args = self.args + " --cc-timely-minrtt={}".format(cc_timely_min_rtt)
            self.args = self.args + " --cc-timely-tlow={}".format(cc_timely_tlow)
            self.args = self.args + " --cc-timely-thigh={}".format(cc_timely_thigh)
            self.args = self.args + " --cc-timely-minrate={}".format(cc_timely_minrate)
            self.args = self.args + " --cc-timely-step={}".format(cc_timely_step)

        self.pane = pane
        self.ip = ip
        self.n_cores = n_cores

class ContainerConfig:
    def __init__(self, pane, machine_config, vtas_dir, vtas_dir_virt, tas_dir, idx, n_cores,
                 memory, n_queues=1, tunnel=False, cset="container"):
        self.name = (
            "server_{}".format(idx)
            if machine_config.is_server
            else "client_{}".format(idx)
        )   

        self.pane = pane
        self.id = idx

        self.n_cores = n_cores
        self.memory = memory
        self.n_queues = n_queues
        self.tunnel = tunnel
        self.cset = cset 

        self.tas_dir = tas_dir
        self.manager_dir = vtas_dir + '/images'
        self.manager_dir_virt = vtas_dir_virt + '/images'

        if machine_config.is_server:
            self.veth_bridge_ip = '192.168.10.{}'.format(20 + idx)
            self.veth_container_ip = '192.168.10.{}'.format(1 + idx)
        else:
            self.veth_bridge_ip = '192.168.10.{}'.format(60 + idx)
            self.veth_container_ip = '192.168.10.{}'.format(20 + idx)

class VMConfig:
    def __init__(self, pane, machine_config, tas_dir, tas_dir_virt, idx,
                 n_cores, memory, 
                 cset="vm",
                 n_queues=None):
        self.name = "server" if machine_config.is_server else "client"
        
        self.manager_dir = tas_dir + '/images'
        self.manager_dir_virt = tas_dir_virt + '/images'
        
        self.pane = pane
        self.id = idx

        self.n_cores = n_cores
        self.cset = cset

        self.memory = memory
        self.n_queues = n_queues
        if machine_config.is_server:
            self.vm_ip = '192.168.10.{}'.format(20 + idx)
            self.tas_tap_ip = '10.0.1.{}'.format(1 + idx)
        else:
            self.vm_ip = '192.168.10.{}'.format(60 + idx)
            self.tas_tap_ip = '10.0.1.{}'.format(20 + idx)

class ProxyConfig:
    def __init__(self, machine_config, comp_dir):
        self.name = "server" if machine_config.is_server else "client"
        
        self.out_dir = comp_dir + "/out"
        
        self.ivshm_socket_path = '/run/tasproxy'
        
        self.comp_dir = comp_dir
        self.comp_cmd = 'make -j6'
        self.clean_cmd = 'make clean'

class HostProxyConfig(ProxyConfig):
    def __init__(self, pane, machine_config, comp_dir, block=0, poll_cycles=10000):
        ProxyConfig.__init__(self, machine_config, comp_dir)
        self.exec_file = self.comp_dir + '/proxy/host/host {} {}'.format(block, poll_cycles)
        
        self.out_file = 'proxy_h'
        self.out = self.out_dir + '/' + self.out_file
        
        self.pane = pane

class GuestProxyConfig(ProxyConfig):
    def __init__(self, pane, machine_config, comp_dir, block=0, poll_cycles=10000):
        ProxyConfig.__init__(self, machine_config, comp_dir)
        self.exec_file = self.comp_dir + '/proxy/guest/guest {} {}'.format(block, poll_cycles)
       
        self.out_file = 'proxy_g'
        self.out = self.out_dir + '/' + self.out_file
       
        self.pane = pane

class ClientConfig:
    def __init__(self, pane, idx, vmid,
            ip, port, ncores, msize, mpending,
            nconns, open_delay, max_msgs_conn, max_pend_conns,
            bench_dir, tas_dir, stack, exp_name, 
            conn_latency=False,
            bursty=False, rate_normal=10000, rate_burst=10000, 
            burst_length=0, burst_interval=0, groupid=0, cset="client"):
        self.name = "client"
        self.exp_name = exp_name
        self.exp_name = ""
        self.tas_dir = tas_dir
       
        self.comp_dir = bench_dir + "/micro_rpc"
        self.comp_cmd = 'make -j6'
        self.clean_cmd = 'make clean'
       
        self.bench_dir = bench_dir
        self.lib_so = tas_dir + '/lib/libtas_interpose.so'

        self.conn_latency = conn_latency

        if bursty:
            self.exec_file = self.comp_dir + '/testclient_linux_bursty'
        elif conn_latency:
            self.exec_file = self.comp_dir + '/testclient_linux_open_latency'
        else:
            self.exec_file = self.comp_dir + '/testclient_linux'

        self.out_dir = tas_dir + "/out"
        self.out_file = "{}_client{}_node{}_nconns{}_ncores{}_msize{}".format(
                exp_name, idx, vmid, nconns, ncores, msize)
        self.hist_file = "hist-" + self.out_file
        self.hist_msgs_file = "histmsgs-" + self.out_file
        self.hist_open_file = "histopen-" + self.out_file
        self.temp_file = "temp"
        self.out = self.out_dir + '/' + self.out_file
        self.hist_out = self.out_dir + "/" + self.hist_file
        self.hist_msgs_out = self.out_dir + "/" + self.hist_msgs_file
        self.hist_open_out = self.out_dir + "/" + self.hist_open_file
       
        if bursty:
            self.args = '{} {} {} foo {} {} {} {} {} {} {} {} {} {}'.format(ip, port, ncores, \
                msize, mpending, nconns, open_delay, \
                max_msgs_conn, max_pend_conns, \
                rate_normal, rate_burst, burst_length, burst_interval)
        else:
            self.args = '{} {} {} foo {} {} {} {} {} {} {} {}'.format(ip, port, ncores, \
                msize, mpending, nconns, open_delay, \
                max_msgs_conn, max_pend_conns, \
                self.out_dir + "/", self.out_file)

        self.cset = cset
        self.groupid = groupid
        self.pane = pane
        self.id = idx
        self.stack = stack

class ServerConfig:
    def __init__(self, pane, idx, vmid,
            port, ncores, max_flows, max_bytes,
            bench_dir, tas_dir, groupid=0, cset="server"):
        self.name = "server"
        self.tas_dir = tas_dir
        
        self.bench_dir = bench_dir
        self.comp_dir = bench_dir + "/micro_rpc"
        self.comp_cmd = 'make -j6'
        self.clean_cmd = 'make clean'
        self.lib_so = tas_dir + '/lib/libtas_interpose.so'
        self.exec_file = self.comp_dir + '/echoserver_linux'
        self.args = '{} {} foo {} {}'.format(port, ncores, \
                max_flows, max_bytes)
        
        self.out_dir = tas_dir + "/out"
        self.out_file = 'rpc_s'
        self.out = self.out_dir + '/' + self.out_file
        
        self.cset = cset
        self.groupid = groupid
        self.pane = pane
        self.id = idx
        self.vmid = vmid
