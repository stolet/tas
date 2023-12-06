import experiments as exp

from exps.sensitivity_budget_lat.configs.virt_tas import Config as TasVirtConf

experiments = []

boosts = [0.94]
max_budgets = [210000, 420000, 630000, 840000, 1050000]
n_runs = 3

for n_r in range(n_runs):
  for boost in boosts:
      for budget in max_budgets:
        exp_name = "sensitivity-budget-lat-run{}-boost{}-budget{}_".format(n_r, boost, budget)
        tas_virt_exp = exp.Experiment(TasVirtConf(exp_name + "virt-tas", boost, budget), name=exp_name)

        experiments.append(tas_virt_exp)
