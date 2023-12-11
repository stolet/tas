import sys
sys.path.append("../../../")

import os
import numpy as np
import pandas as pd
import experiments.plot_utils as putils


def check_boost(data, boost):
  if boost not in data:
    data[boost] = {}

def check_stack(data, boost, stack):
  if stack not in data[boost]:
    data[boost][stack] = {}

def check_run(data, boost, stack, run):
  if run not in data[boost][stack]:
    data[boost][stack][run] = {}

def check_nid(data, boost, stack, run, nid):
  if nid not in data[boost][stack][run]:
    data[boost][stack][run][nid] = {}

def check_cid(data, boost, stack, run, nid, cid):
  if cid not in data[boost][stack][run][nid]:
    data[boost][stack][run][nid][cid] = ""

def get_avg_tp_victim(fname_c0, fname_c1):
  n_messages = 0
  n = 0

  f = open(fname_c0)
  lines = f.readlines()

  c1_first_ts = putils.get_first_ts(fname_c1)
  idx, _ = putils.get_min_idx(fname_c0, c1_first_ts)

  first_line = lines[idx]
  last_line = lines[len(lines) - 1]

  n_messages = int(putils.get_n_messages(last_line)) - \
      int(putils.get_n_messages(first_line))
  n = len(lines) - idx

  return (n_messages / n) * 1024 * 8

# NOTE: Aggressor has different message size (1024 vs 64 bytes)
def get_avg_tp_aggr(fname_c0, fname_c1):
  n_messages = 0
  n = 0

  f = open(fname_c1)
  lines = f.readlines()

  c1_first_ts = putils.get_first_ts(fname_c1)
  c0_idx, c0_ts = putils.get_min_idx(fname_c0, c1_first_ts)
  c1_idx, c1_ts = putils.get_min_idx(fname_c1, c0_ts)

  first_line = lines[c1_idx]
  last_line = lines[len(lines) - 1]

  n_messages = int(putils.get_n_messages(last_line)) - \
      int(putils.get_n_messages(first_line))
  n = len(lines) - c1_idx

  return (n_messages / n) * 1024 * 8

def parse_metadata():
  dir_path = "./out/"
  data = {}

  putils.remove_cset_dir(dir_path)
  for f in os.listdir(dir_path):
    fname = os.fsdecode(f)
    if "tas_c" == fname or "hist" in fname:
      continue

    run = putils.get_expname_run(fname)
    boost = str(float(putils.get_expname_boost(fname)))
    cid = putils.get_client_id(fname)
    nid = putils.get_node_id(fname)
    stack = putils.get_stack(fname)

    check_boost(data, boost)
    check_stack(data, boost, stack)
    check_run(data, boost, stack, run)
    check_nid(data, boost, stack, run, nid)
    check_cid(data, boost, stack, run, nid, cid)

    data[boost][stack][run][nid][cid] = fname

  return data

def parse_data(parsed_md):
  data = {}
  out_dir = "./out/"
  for boost in parsed_md:
    data_point = {}
    for stack in parsed_md[boost]:
      tp_x_victim = np.array([])
      tp_x_aggr = np.array([])
      for run in parsed_md[boost][stack]:
        fname_c0 = out_dir + parsed_md[boost][stack][run]['0']['0']
        fname_c1 = out_dir + parsed_md[boost][stack][run]['1']['0']
        tp_victim = get_avg_tp_victim(fname_c0, fname_c1)
        tp_aggr = get_avg_tp_aggr(fname_c0, fname_c1)
        # if tp_victim > 0 and tp_aggr > 0:
        tp_x_victim = np.append(tp_x_victim, tp_victim)
        tp_x_aggr = np.append(tp_x_aggr, tp_aggr)
      data_point[stack] = {
        "tp-victim": tp_x_victim.mean(),
        "tp-victim-std": tp_x_victim.std(),
        "tp-aggr": tp_x_aggr.mean(),
        "tp-aggr-std": tp_x_aggr.std(),
      }
    data[boost] = data_point
  
  return data

def save_dat_file(data):
  header = "boost virt-tas-victim-avg virt-tas-victim-std virt-tas-aggr-avg virt-tas-aggr-std\n"
  boosts = list(data.keys())
  boosts = list(map(str, sorted(map(float, boosts))))

  fname = "./tp.dat"
  f_lat = open(fname, "w+")
  f_lat.write(header)

  for boost in boosts:
    f_lat.write("{} {} {} {} {}\n".format(
      float(boost),
      data[boost]['virt-tas']["tp-victim"],
      data[boost]['virt-tas']["tp-victim-std"],
      data[boost]['virt-tas']["tp-aggr"],
      data[boost]['virt-tas']["tp-aggr-std"],
      ))
    
def main():
  parsed_md = parse_metadata()
  latencies = parse_data(parsed_md)
  save_dat_file(latencies)

if __name__ == '__main__':
  main()