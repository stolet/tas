import experiments as exp

from exps.perf_iso_latconn.configs.bare_tas import Config as TasBareConf
from exps.perf_iso_latconn.configs.bare_vtas import Config as VTasBareConf
from exps.perf_iso_latconn.configs.virt_tas import Config as TasVirtConf
from exps.perf_iso_latconn.configs.ovs_linux import Config as OVSLinuxConf
from exps.perf_iso_latconn.configs.ovs_tas import Config as OVSTasConf

experiments = []

nconns = 500
n_cores = [5,4,3,2,1]
n_runs = 3

for n_r in range(n_runs):
  for n_c in n_cores:
    exp_name = "perf-iso-latconn-run{}-conns{}_".format(n_r, n_c * nconns)
    tas_bare_exp = exp.Experiment(TasBareConf(exp_name + "bare-tas", n_c, nconns), name=exp_name)
    vtas_bare_exp = exp.Experiment(VTasBareConf(exp_name + "bare-vtas", n_c, nconns), name=exp_name)
    tas_virt_exp = exp.Experiment(TasVirtConf(exp_name + "virt-tas", n_c, nconns), name=exp_name)
    ovs_linux_exp = exp.Experiment(OVSLinuxConf(exp_name + "ovs-linux", n_c, nconns), name=exp_name)
    ovs_tas_exp = exp.Experiment(OVSTasConf(exp_name + "ovs-tas", n_c, nconns), name=exp_name)

    experiments.append(tas_bare_exp)
    experiments.append(tas_virt_exp)
    experiments.append(ovs_tas_exp)
    experiments.append(ovs_linux_exp)
