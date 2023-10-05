import experiments as exp

from exps.overhead_throughput_msize.configs.bare_tas import Config as TasBareConf
from exps.overhead_throughput_msize.configs.bare_vtas import Config as VTasBareConf
from exps.overhead_throughput_msize.configs.virt_tas import Config as TasVirtConf
from exps.overhead_throughput_msize.configs.bare_linux import Config as BareLinuxConf
from exps.overhead_throughput_msize.configs.ovs_tas import Config as OVSTasConf
from exps.overhead_throughput_msize.configs.ovs_linux import Config as OVSLinuxConf
from exps.overhead_throughput_msize.configs.container_tas import Config as ContainerTasConf
from exps.overhead_throughput_msize.configs.container_virtuoso import Config as ContainerVirtuosoConf
from exps.overhead_throughput_msize.configs.container_ovs_dpdk import Config as ContainerOVSDPDK

experiments = []

msize = [1024, 512, 256, 128, 64]
n_runs = 1

for n_r in range(n_runs):
  for n_m in msize:
    exp_name = "overhead-throughputmsgs-run{}-msize{}_".format(n_r, n_m)
    tas_bare_exp = exp.Experiment(TasBareConf(exp_name + "bare-tas", n_m), name=exp_name)
    vtas_bare_exp = exp.Experiment(VTasBareConf(exp_name + "bare-vtas", n_m), name=exp_name)
    tas_virt_exp = exp.Experiment(TasVirtConf(exp_name + "virt-tas", n_m), name=exp_name)
    ovs_tas_exp = exp.Experiment(OVSTasConf(exp_name + "ovs-tas", n_m), name=exp_name)
    bare_linux_exp = exp.Experiment(BareLinuxConf(exp_name + "bare-linux", n_m), name=exp_name)
    ovs_linux_exp = exp.Experiment(OVSLinuxConf(exp_name + "ovs-linux", n_m), name=exp_name)
    container_tas_exp = exp.Experiment(ContainerTasConf(exp_name + "container-tas", n_m), name=exp_name)
    container_virtuoso_exp = exp.Experiment(ContainerVirtuosoConf(exp_name + "container-virtuoso", n_m), name=exp_name)
    container_ovs_dpdk_exp = exp.Experiment(ContainerOVSDPDK(exp_name + "container-ovsdpdk", n_m), name=exp_name)

    # experiments.append(tas_bare_exp)
    # experiments.append(tas_virt_exp)
    # experiments.append(ovs_tas_exp)
    # experiments.append(bare_linux_exp)
    # experiments.append(ovs_linux_exp)
    experiments.append(container_virtuoso_exp)
    experiments.append(container_tas_exp)
    experiments.append(container_ovs_dpdk_exp)
