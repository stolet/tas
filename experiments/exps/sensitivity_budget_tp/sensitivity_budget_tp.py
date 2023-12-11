import experiments as exp

from exps.sensitivity_budget_tp.configs.virt_tas import Config as TasVirtConf

experiments = []

boosts = [0.94]
max_budgets = [210000,2100000,21000000,210000000,2100000000]
n_runs = 3

for n_r in range(n_runs):
  for boost in boosts:
      for budget in max_budgets:
        exp_name = "sensitivity-budget-tp-run{}-boost{}-budget{}_".format(n_r, boost, budget)
        tas_virt_exp = exp.Experiment(TasVirtConf(exp_name + "virt-tas", boost, budget), name=exp_name)

        experiments.append(tas_virt_exp)
