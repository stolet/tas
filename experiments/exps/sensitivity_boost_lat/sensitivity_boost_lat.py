import experiments as exp

from exps.sensitivity_boost_lat.configs.virt_tas import Config as TasVirtConf
from exps.sensitivity_boost_lat.configs.ovs_tas import Config as OvSTasConf

experiments = []

boosts = [0.25, 0.5, 0.75, 1, 1.25, 1.5, 1.75, 2]
max_budgets = [210000]
n_runs = 3

for n_r in range(n_runs):
  for boost in boosts:
      for budget in max_budgets:
        exp_name = "sensitivity-boost-lat-run{}-boost{}-budget{}_".format(n_r, boost, budget)
        tas_virt_exp = exp.Experiment(TasVirtConf(exp_name + "virt-tas", boost, budget), name=exp_name)
        ovs_tas_exp = exp.Experiment(OvSTasConf(exp_name + "ovs-tas", boost, budget), name=exp_name)

        experiments.append(tas_virt_exp)
        # experiments.append(ovs_tas_exp)
