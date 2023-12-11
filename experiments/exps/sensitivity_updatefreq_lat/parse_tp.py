import sys
sys.path.append("../../../")

import os
import numpy as np
import pandas as pd
import experiments.plot_utils as putils


def check_freq(data, freq):
  if freq not in data:
    data[freq] = {}

def check_stack(data, freq, stack):
  if stack not in data[freq]:
    data[freq][stack] = {}

def check_run(data, freq, stack, run):
  if run not in data[freq][stack]:
    data[freq][stack][run] = {}

def check_nid(data, freq, stack, run, nid):
  if nid not in data[freq][stack][run]:
    data[freq][stack][run][nid] = {}

def check_cid(data, freq, stack, run, nid, cid):
  if cid not in data[freq][stack][run][nid]:
    data[freq][stack][run][nid][cid] = ""

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

  return n_messages / n

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
    freq = str(float(putils.get_expname_freq(fname)))
    cid = putils.get_client_id(fname)
    nid = putils.get_node_id(fname)
    stack = putils.get_stack(fname)

    check_freq(data, freq)
    check_stack(data, freq, stack)
    check_run(data, freq, stack, run)
    check_nid(data, freq, stack, run, nid)
    check_cid(data, freq, stack, run, nid, cid)

    data[freq][stack][run][nid][cid] = fname

  return data

def parse_data(parsed_md):
  data = {}
  out_dir = "./out/"
  for freq in parsed_md:
    data_point = {}
    for stack in parsed_md[freq]:
      tp_x_victim = np.array([])
      tp_x_aggr = np.array([])
      for run in parsed_md[freq][stack]:
        fname_c0 = out_dir + parsed_md[freq][stack][run]['0']['0']
        fname_c1 = out_dir + parsed_md[freq][stack][run]['1']['0']
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
    data[freq] = data_point
  
  return data

def save_dat_file(data):
  header = "freq virt-tas-victim-avg virt-tas-victim-std virt-tas-aggr-avg virt-tas-aggr-std\n"
  freqs = list(data.keys())
  freqs = list(map(str, sorted(map(float, freqs))))

  fname = "./tp.dat"
  f_lat = open(fname, "w+")
  f_lat.write(header)

  for freq in freqs:
    f_lat.write("{} {} {} {} {}\n".format(
      float(freq),
      data[freq]['virt-tas']["tp-victim"],
      data[freq]['virt-tas']["tp-victim-std"],
      data[freq]['virt-tas']["tp-aggr"],
      data[freq]['virt-tas']["tp-aggr-std"],
      ))
    
def main():
  parsed_md = parse_metadata()
  latencies = parse_data(parsed_md)
  save_dat_file(latencies)

if __name__ == '__main__':
  main()