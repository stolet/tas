import experiments as exp

from exps.bursty_resource_efficiency.configs.virt_tas import Config as TasVirtConf
from exps.bursty_resource_efficiency.configs.ovs_tas import Config as OVSTasConf

experiments = []
n_runs = 1
fp_cores = [3]

# 25% Overlap
open_delays = [10, 40, 40, 40]
for n_r in range(n_runs):
  for n_c in fp_cores:
    exp_name = "bursty-resource-efficiency-overlap25-run{}-fpcores{}_".format(n_r, n_c)
    tas_virt_exp = exp.Experiment(TasVirtConf(exp_name + "virt-tas", n_c, open_delays), name=exp_name)
    experiments.append(tas_virt_exp)

  exp_name = "bursty-resource-efficiency-overlap25-run{}-fpcores{}_".format(n_r, 1)
  ovs_tas_exp = exp.Experiment(OVSTasConf(exp_name + "ovs-tas", 1, open_delays), name=exp_name)
  experiments.append(ovs_tas_exp)

# 50% Overlap
open_delays = [10, 10, 40, 40]
for n_r in range(n_runs):
  for n_c in fp_cores:
    exp_name = "bursty-resource-efficiency-overlap50-run{}-fpcores{}_".format(n_r, n_c)
    tas_virt_exp = exp.Experiment(TasVirtConf(exp_name + "virt-tas", n_c, open_delays), name=exp_name)
    experiments.append(tas_virt_exp)

  exp_name = "bursty-resource-efficiency-overlap50-run{}-fpcores{}_".format(n_r, 1)
  ovs_tas_exp = exp.Experiment(OVSTasConf(exp_name + "ovs-tas", 1, open_delays), name=exp_name)
  experiments.append(ovs_tas_exp)

# 75% Overlap
n_runs = 1
open_delays = [10, 10, 10, 40]
for n_r in range(n_runs):
  for n_c in fp_cores:
    exp_name = "bursty-resource-efficiency-overlap75-run{}-fpcores{}_".format(n_r, n_c)
    tas_virt_exp = exp.Experiment(TasVirtConf(exp_name + "virt-tas", n_c, open_delays), name=exp_name)
    experiments.append(tas_virt_exp)
  
  exp_name = "bursty-resource-efficiency-overlap75-run{}-fpcores{}_".format(n_r, 1)
  ovs_tas_exp = exp.Experiment(OVSTasConf(exp_name + "ovs-tas", 1, open_delays), name=exp_name)
  experiments.append(ovs_tas_exp)

# 100% Overlap
n_runs = 1
open_delays = [10, 10, 10, 10]
for n_r in range(n_runs):
  for n_c in fp_cores:
    exp_name = "bursty-resource-efficiency-overlap100-run{}-fpcores{}_".format(n_r, n_c)
    tas_virt_exp = exp.Experiment(TasVirtConf(exp_name + "virt-tas", n_c, open_delays), name=exp_name)
    experiments.append(tas_virt_exp)

  exp_name = "bursty-resource-efficiency-overlap100-run{}-fpcores{}_".format(n_r, 1)
  ovs_tas_exp = exp.Experiment(OVSTasConf(exp_name + "ovs-tas", 1, open_delays), name=exp_name)
  experiments.append(ovs_tas_exp)







