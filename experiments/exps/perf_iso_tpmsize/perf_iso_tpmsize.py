import experiments as exp

from exps.perf_iso_tpmsize.configs.bare_tas import Config as TasBareConf
from exps.perf_iso_tpmsize.configs.bare_vtas import Config as VTasBareConf
from exps.perf_iso_tpmsize.configs.virt_tas import Config as TasVirtConf
from exps.perf_iso_tpmsize.configs.ovs_linux import Config as OVSLinuxConf
from exps.perf_iso_tpmsize.configs.ovs_tas import Config as OVSTasConf

experiments = []

msize = [1024,512,256,128,64]
n_runs = 3

for n_r in range(n_runs):
  for n_m in msize:
    exp_name = "perf-iso-tpmsize-run{}-msize{}_".format(n_r, n_m)
    tas_bare_exp = exp.Experiment(TasBareConf(exp_name + "bare-tas", n_m), name=exp_name)
    vtas_bare_exp = exp.Experiment(VTasBareConf(exp_name + "bare-vtas", n_m), name=exp_name)
    tas_virt_exp = exp.Experiment(TasVirtConf(exp_name + "virt-tas", n_m), name=exp_name)
    ovs_linux_exp = exp.Experiment(OVSLinuxConf(exp_name + "ovs-linux", n_m), name=exp_name)
    ovs_tas_exp = exp.Experiment(OVSTasConf(exp_name + "ovs-tas", n_m), name=exp_name)

    experiments.append(tas_bare_exp)
    experiments.append(tas_virt_exp)
    experiments.append(ovs_tas_exp)
    experiments.append(ovs_linux_exp)  
