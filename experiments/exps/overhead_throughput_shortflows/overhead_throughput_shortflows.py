import experiments as exp

from exps.overhead_throughput_shortflows.configs.bare_tas import Config as TasBareConf
from exps.overhead_throughput_shortflows.configs.virt_tas import Config as TasVirtConf
from exps.overhead_throughput_shortflows.configs.ovs_tas import Config as OVSTasConf
from exps.overhead_throughput_shortflows.configs.bare_linux import Config as BareLinuxConf
from exps.overhead_throughput_shortflows.configs.ovs_linux import Config as OVSLinuxConf
from exps.overhead_throughput_shortflows.configs.container_tas import Config as ContainerTasConf
from exps.overhead_throughput_shortflows.configs.container_virtuoso import Config as ContainerVirtuosoConf
from exps.overhead_throughput_shortflows.configs.container_ovs_dpdk import Config as ContainerOVSDPDK

experiments = []

# flow_lens = [1024, 512, 256, 128, 64, 1]
flow_lens = [128, 64, 1]
n_runs = 1

# Run these commands if running out of ephemeral ports: 
#   sudo sysctl -w net.ipv4.tcp_tw_reuse=1
#   sudo sysctl -w net.ipv4.tcp_fin_timeout=1
# Defaults:
#   net.ipv4.tcp_tw_reuse=2
#   net.ipv4.tcp_fin_timeout = 60
#   net.ipv4.ip_local_port_range = 32768	60999

for n_r in range(n_runs):
  for flow_len in flow_lens:
    exp_name = "overhead-tpflows-run{}-flowlen{}_".format(n_r, flow_len)
    tas_bare_exp = exp.Experiment(TasBareConf(exp_name + "bare-tas", flow_len), name=exp_name)
    tas_virt_exp = exp.Experiment(TasVirtConf(exp_name + "virt-tas", flow_len), name=exp_name)
    ovs_tas_exp = exp.Experiment(OVSTasConf(exp_name + "ovs-tas", flow_len), name=exp_name)
    bare_linux_exp = exp.Experiment(BareLinuxConf(exp_name + "bare-linux", flow_len), name=exp_name)
    ovs_linux_exp = exp.Experiment(OVSLinuxConf(exp_name + "ovs-linux", flow_len), name=exp_name)
    container_tas_exp = exp.Experiment(ContainerTasConf(exp_name + "container-tas", flow_len), name=exp_name)
    container_virtuoso_exp = exp.Experiment(ContainerVirtuosoConf(exp_name + "container-virtuoso", flow_len), name=exp_name)
    container_ovs_dpdk_exp = exp.Experiment(ContainerOVSDPDK(exp_name + "container-ovsdpdk", flow_len), name=exp_name)

    # experiments.append(tas_bare_exp)
    # experiments.append(tas_virt_exp)
    # experiments.append(ovs_tas_exp)
    # experiments.append(bare_linux_exp)
    # experiments.append(ovs_linux_exp)
    # experiments.append(container_ovs_dpdk_exp)
    # experiments.append(container_virtuoso_exp)
    experiments.append(container_tas_exp)


