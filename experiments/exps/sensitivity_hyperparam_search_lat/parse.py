import sys
sys.path.append("../../../")

import os
import numpy as np
import pandas as pd
import experiments.plot_utils as putils


def check_boost(data, boost):
  if boost not in data:
    data[boost] = {}

def check_budget(data, boost, budget):
  if budget not in data[boost]:
    data[boost][budget] = {}

def check_stack(data, boost, budget, stack):
  if stack not in data[boost][budget]:
    data[boost][budget][stack] = {}

def check_run(data, boost, budget, stack, run):
  if run not in data[boost][budget][stack]:
    data[boost][budget][stack][run] = {}

def check_nid(data, boost, budget, stack, run, nid):
  if nid not in data[boost][budget][stack][run]:
    data[boost][budget][stack][run][nid] = {}

def check_cid(data, boost, budget, stack, run, nid, cid):
  if cid not in data[boost][budget][stack][run][nid]:
    data[boost][budget][stack][run][nid][cid] = ""

def get_avg_tp(fname_c1, fname_c0):
  n_messages = 0
  n = 0

  f = open(fname_c1)
  lines = f.readlines()

  c1_first_ts = putils.get_first_ts(fname_c0)
  idx, _ = putils.get_min_idx(fname_c1, c1_first_ts)

  first_line = lines[idx]
  last_line = lines[len(lines) - 1]

  n_messages = int(putils.get_n_messages(last_line)) - \
      int(putils.get_n_messages(first_line))
  n = len(lines) - idx

  return n_messages / n

def parse_metadata():
  dir_path = "./out/"
  data = {}

  putils.remove_cset_dir(dir_path)
  for f in os.listdir(dir_path):
    fname = os.fsdecode(f)
    if "tas_c" == fname or "hist" in fname:
      continue

    run = putils.get_expname_run(fname)
    boost = str(float(putils.get_expname_boost_(fname)))
    budget = str(int(putils.get_expname_budget(fname)))
    cid = putils.get_client_id(fname)
    nid = putils.get_node_id(fname)
    stack = putils.get_stack(fname)

    check_boost(data, boost)
    check_budget(data, boost, budget)
    check_stack(data, boost, budget, stack)
    check_run(data, boost, budget, stack, run)
    check_nid(data, boost, budget, stack, run, nid)
    check_cid(data, boost, budget, stack, run, nid, cid)

    data[boost][budget][stack][run][nid][cid] = fname

  return data

def parse_data(parsed_md):
  data = {}
  out_dir = "./out/"
  for boost in parsed_md:
    data[boost] = {}
    for budget in parsed_md[boost]:
      data_point = {}
      for stack in parsed_md[boost][budget]:
        tp_x = np.array([])
        latencies = putils.init_latencies()
        for run in parsed_md[boost][budget][stack]:
          fname_c0 = out_dir + parsed_md[boost][budget][stack][run]['0']['0']
          fname_c1 = out_dir + parsed_md[boost][budget][stack][run]['1']['0']
          putils.append_latencies(latencies, fname_c0)
          tp = get_avg_tp(fname_c1, fname_c0)
          if tp > 0:
            tp_x = np.append(tp_x, tp)

        data_point[stack] = {
          "tp": tp_x.mean(),
          "tp-std": tp_x.std(),
          "lat": putils.get_latency_avg(latencies),
          "lat-std": putils.get_latency_std(latencies)
        }
      data[boost][budget] = data_point
  
  return data

def save_dat_file(data):
  boosts = list(data.keys())
  boosts = list(map(str, sorted(map(float, boosts))))
  budgets = list(data[boosts[0]].keys())
  budgets = list(map(str, sorted(map(int, budgets))))
  stacks =  list(data[boosts[0]][budgets[0]].keys())
  percentiles =  list(data[boosts[0]][budgets[0]][stacks[0]]['lat'].keys())

  matrix = np.zeros((len(budgets), len(boosts)))

  for percentile in percentiles:
      fname_lat = "./lat_{}.dat".format(percentile)

      for i, boost in enumerate(boosts):
        for j, budget in enumerate(budgets):
          matrix[j][i] = data[boost][budget]['virt-tas']['lat'][percentile]
      
      df = pd.DataFrame(matrix, index=budgets, columns=boosts)
      df.to_csv(fname_lat, sep=" ")
  
  fname_tp = "./tp.dat"

  for i, boost in enumerate(boosts):
    for j, budget in enumerate(budgets):
      matrix[j][i] = data[boost][budget]['virt-tas']['tp']
  
  df = pd.DataFrame(matrix, index=budgets, columns=boosts)
  df.to_csv(fname_tp, sep=" ")

def main():
  parsed_md = parse_metadata()
  latencies = parse_data(parsed_md)
  save_dat_file(latencies)

if __name__ == '__main__':
  main()