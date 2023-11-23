import sys
sys.path.append("../../../")

import os
import math
import numpy as np
import experiments.plot_utils as putils
import bisect

TSC_DIV = 100000

def check_n_vms(data, n_vms):
  if n_vms not in data:
    data[n_vms] = {}

def check_stack(data, n_vms, stack):
  if stack not in data[n_vms]:
    data[n_vms][stack] = {}

def check_run(data, n_vms, stack, run):
  if run not in data[n_vms][stack]:
    data[n_vms][stack][run] = {}

def check_nid(data, n_vms, stack, run, nid):
  if nid not in data[n_vms][stack][run]:
    data[n_vms][stack][run][nid] = {}

def check_cid(data, n_vms, stack, run, nid, cid):
  if cid not in data[n_vms][stack][run][nid]:
    data[n_vms][stack][run][nid][cid] = ""

def get_avg_tp(fname, start_ts, end_ts):
  n_messages = 0
  n = 0

  f = open(fname)
  lines = f.readlines()

  start_idx, _ = putils.get_min_idx(fname, start_ts)
  end_idx, _ = putils.get_min_idx(fname, end_ts)

  first_line = lines[start_idx]
  last_line = lines[end_idx]

  n_messages = int(putils.get_n_messages(last_line)) - \
      int(putils.get_n_messages(first_line))
  msize = int(putils.get_msize(fname))
  assert(end_idx >= start_idx)
  n = end_idx - start_idx

  return n_messages / n
  # return (n_messages * msize * 8 / n) / 1000000

def get_earliest_end(fnames):
  vmid = -1
  earliest_ts = math.inf

  for i in range(len(fnames)):
    fname = fnames[i]
    f = open(fname)
    lines = f.readlines()
    ts = int(putils.get_ts(lines[len(lines) - 1]))
    
    if ts < earliest_ts:
      earliest_ts = ts
      vmid = i

  assert(vmid >= 0)

  return vmid, earliest_ts

def get_latest_start(fnames):
  vmid = -1
  latest_ts = -1

  for i in range(len(fnames)):
    fname = fnames[i]
    f = open(fname)
    lines = f.readlines()
    ts = int(putils.get_ts(lines[0]))
    
    if ts > latest_ts:
      latest_ts = ts
      vmid = i

  assert(vmid >= 0)

  return vmid, latest_ts


def parse_metadata():
  dir_path = "./out/"
  data = {}

  for f in os.listdir(dir_path):
    fname = os.fsdecode(f)

    if "tas_c" == fname or "hist" in fname:
      continue

    run = putils.get_expname_run(fname)
    n_vms = putils.get_expname_n_vms(fname)
    cid = putils.get_client_id(fname)
    nid = putils.get_node_id(fname)
    stack = putils.get_stack(fname)

    check_n_vms(data, n_vms)
    check_stack(data, n_vms, stack)
    check_run(data, n_vms, stack, run)
    check_nid(data, n_vms, stack, run, nid)
    check_cid(data, n_vms, stack, run, nid, cid)

    data[n_vms][stack][run][nid][cid] = fname

  return data

def parse_data(parsed_md):
  data = []
  out_dir = "./out/"
  for n_vms in parsed_md:
    data_point = {"n_vms": n_vms}
    for stack in parsed_md[n_vms]:
      tp_x = np.array([])
      for run in parsed_md[n_vms][stack]:
        fnames = []
        for vmid in range(int(n_vms)):
          fname = out_dir + parsed_md[n_vms][stack][run][str(vmid)]["0"]
          fnames.append(fname)

        start_vm, start_ts = get_latest_start(fnames)
        end_vm, end_ts = get_earliest_end(fnames)

        tp = 0
        for i in range(int(n_vms)):
          fname = fnames[i]
          tp += get_avg_tp(fname, start_ts, end_ts)

        if tp > 0:
          tp_x = np.append(tp_x, tp)

        data_point[stack] = {
          "tp": tp_x.mean(),
          "std": tp_x.std(),
        }
  
    data.append(data_point)
  
  data = sorted(data, key=lambda d: int(d['n_vms']))
  return data

def save_dat_file(data, fname):
  f = open(fname, "w+")
  header = "n_vms " + \
      "virt-tas-avg ovs-tas-avg ovs-linux-avg " + \
      "virt-tas-std ovs-tas-std ovs-linux-std\n"
  f.write(header)
  for dp in data:
    f.write("{} {} {} {} {} {} {}\n".format(
      dp["n_vms"],
      dp["virt-tas"]["tp"], dp["ovs-tas"]["tp"], dp["ovs-linux"]["tp"],
      dp["virt-tas"]["std"], dp["ovs-tas"]["std"], dp["ovs-linux"]["std"]))
        
def main():
  parsed_md = parse_metadata()
  data = parse_data(parsed_md)

  save_dat_file(data, "./tp.dat")

if __name__ == '__main__':
  main()