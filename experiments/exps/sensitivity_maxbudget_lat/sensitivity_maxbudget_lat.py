import experiments as exp

from exps.sensitivity_hyperparam_search.configs.virt_tas import Config as TasVirtConf

experiments = []

max_budgets = [2625000, 2362500, 2100000, 1837500, 1575000, 1312500, 1050000, 787500, 525000]
n_runs = 3

for n_r in range(n_runs):
  for mb in max_budgets:
    exp_name = "sensitivity-maxbudget-run{}-budget{}_".format(n_r, mb)
    tas_virt_exp = exp.Experiment(TasVirtConf(exp_name + "virt-tas", mb), name=exp_name)

    experiments.append(tas_virt_exp)
