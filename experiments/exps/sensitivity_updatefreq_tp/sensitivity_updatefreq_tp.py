import experiments as exp

from exps.sensitivity_updatefreq_tp.configs.virt_tas import Config as TasVirtConf

experiments = []

freqs = [1000000, 100000, 10000, 1000, 100]
n_runs = 3

for n_r in range(n_runs):
  for freq in freqs:
      exp_name = "sensitivity-updatefreq-tp-run{}-frq{}_".format(n_r, freq)
      tas_virt_exp = exp.Experiment(TasVirtConf(exp_name + "virt-tas", freq), name=exp_name)
      experiments.append(tas_virt_exp)
