import experiments as exp

from exps.sensitivity_hyperparam_search_lat.configs.virt_tas import Config as TasVirtConf

experiments = []

boosts = [2, 1.75, 1.5, 1.25, 1, 0.75, 0.5, 0.25]
max_budgets = [2625000, 2362500, 2100000, 1837500, 1575000, 1312500, 1050000, 787500, 525000]
n_runs = 1

for n_r in range(n_runs):
  for boost in boosts:
      for budget in max_budgets:
        exp_name = "sensitivity-hyperparam-search-lat-run{}-boost{}-budget{}_".format(2, boost, budget)
        tas_virt_exp = exp.Experiment(TasVirtConf(exp_name + "virt-tas", boost, budget), name=exp_name)

        experiments.append(tas_virt_exp)
