import experiments as exp

from exps.sensitivity_boost_lat.configs.virt_tas import Config as TasVirtConf

experiments = []

boosts = [2, 1.75, 1.5, 1.25, 1, 0.75, 0.5, 0.25]
n_runs = 3

for n_r in range(n_runs):
  for boost in boosts:
      exp_name = "sensitivity-boost-lat-run{}-boost{}_".format(n_r, boost)
      tas_virt_exp = exp.Experiment(TasVirtConf(exp_name + "virt-tas", boost), name=exp_name)

      experiments.append(tas_virt_exp)
