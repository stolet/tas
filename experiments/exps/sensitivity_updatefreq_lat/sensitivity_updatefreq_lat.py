import experiments as exp

from exps.sensitivity_updatefreq_lat.configs.virt_tas import Config as TasVirtConf

experiments = []

freqs = [100, 1000, 10000, 100000, 1000000]
n_runs = 1

for n_r in range(n_runs):
  for freq in freqs:
    exp_name = "sensitivity-update-run{}-frq{}_".format(n_r, freq)
    tas_virt_exp = exp.Experiment(TasVirtConf(exp_name + "virt-tas", freq), name=exp_name)

    experiments.append(tas_virt_exp)
