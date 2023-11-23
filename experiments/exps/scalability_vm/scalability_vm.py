import experiments as exp

from exps.scalability_vm.configs.virt_tas import Config as TasVirtConf
from exps.scalability_vm.configs.ovs_tas import Config as OVSTasConf
from exps.scalability_vm.configs.ovs_linux import Config as OVSLinuxConf

experiments = []
n_runs = 1
n_vms = [3, 6, 9, 12, 15, 18]

for n_r in range(n_runs):
  for n_v in n_vms:
    exp_name = "scalability-vm-run{}-vms{}_".format(n_r, n_v)
    tas_virt_exp = exp.Experiment(TasVirtConf(exp_name + "virt-tas", n_v), name=exp_name)
    ovs_tas_exp = exp.Experiment(OVSTasConf(exp_name + "ovs-tas", n_v), name=exp_name)
    ovs_linux_exp = exp.Experiment(OVSLinuxConf(exp_name + "ovs-linux", n_v), name=exp_name)

    # experiments.append(ovs_linux_exp)
    experiments.append(ovs_tas_exp)
    # experiments.append(tas_virt_exp)
