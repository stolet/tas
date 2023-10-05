import sys
sys.path.append("../../../")

import os
import numpy as np
import experiments.plot_utils as putils

def check_flowlen(data, flowlen):
  if flowlen not in data:
    data[flowlen] = {}

def check_stack(data, flowlen, stack):
  if stack not in data[flowlen]:
    data[flowlen][stack] = {}

def check_run(data, flowlen, stack, run):
  if run not in data[flowlen][stack]:
    data[flowlen][stack][run] = {}

def check_nid(data, flowlen, stack, run, nid):
  if nid not in data[flowlen][stack][run]:
    data[flowlen][stack][run][nid] = {}

def check_cid(data, flowlen, stack, run, nid, cid):
  if cid not in data[flowlen][stack][run][nid]:
    data[flowlen][stack][run][nid][cid] = ""

def get_avg_tp(fname_c0):
  n_messages = 0
  n = 0

  f = open(fname_c0)
  lines = f.readlines()

  first_line = lines[0]
  last_line = lines[len(lines) - 1]
    
  n_messages = int(putils.get_n_messages(last_line)) - \
      int(putils.get_n_messages(first_line))
  msize = int(putils.get_msize(fname_c0))
  n = len(lines)

  return n_messages / n
  # return (n_messages * msize * 8 / n) / 1000000

def parse_metadata():
  dir_path = "./out/"
  data = {}

  putils.remove_cset_dir(dir_path)
  for f in os.listdir(dir_path):
    fname = os.fsdecode(f)

    if "tas_c" == fname or "latency_hist" in fname:
      continue

    run = putils.get_expname_run(fname)
    flowlen = putils.get_expname_flowlen(fname)
    cid = putils.get_client_id(fname)
    nid = putils.get_node_id(fname)
    stack = putils.get_stack(fname)

    check_flowlen(data, flowlen)
    check_stack(data, flowlen, stack)
    check_run(data, flowlen, stack, run)
    check_nid(data, flowlen, stack, run, nid)
    check_cid(data, flowlen, stack, run, nid, cid)

    data[flowlen][stack][run][nid][cid] = fname

  return data

def parse_data(parsed_md):
  data = []
  out_dir = "./out/"

  # putils.remove_cset_dir(out_dir)
  for flowlen in parsed_md:
    data_point = {"flowlen": flowlen}
    for stack in parsed_md[flowlen]:
      tp_x = np.array([])
      for run in parsed_md[flowlen][stack]:
        is_virt = stack == "virt-tas" or stack == "ovs-tas" or stack == "ovs-linux"
        if is_virt:
          c0_fname = out_dir + parsed_md[flowlen][stack][run]["0"]["0"]
        else:
          c0_fname = out_dir + parsed_md[flowlen][stack][run]["0"]["0"]

        tp = get_avg_tp(c0_fname)
        if tp > 0:
          tp_x = np.append(tp_x, tp)

      data_point[stack] = {
        "tp": tp_x.mean(),
        "std": tp_x.std(),
      }
  
    data.append(data_point)
  
  data = sorted(data, key=lambda d: int(d['flowlen']))
  
  return data

def save_dat_file(data, fname):
  f = open(fname, "w+")
  header = "flowlen " + \
      "bare-tas-avg virt-tas-avg ovs-tas-avg bare-linux-avg ovs-linux-avg " + \
      "container-tas-avg container-virtuoso-avg container-linux-avg " + \
      "bare-tas-std virt-tas-std ovs-tas-std bare-linux-std ovs-linux-std " + \
      "container-tas-std container-virtuoso-std container-linux-std\n"

  f.write(header)
  for dp in data:
    f.write("{} {} {} {} {} {} {} {} {} {} {} {} {} {} {} {} {}\n".format(
      dp["flowlen"],
      dp["bare-tas"]["tp"], dp["virt-tas"]["tp"],
      dp["ovs-tas"]["tp"], dp["bare-linux"]["tp"], dp["ovs-linux"]["tp"],
      dp["container-tas"]["tp"], dp["container-virtuoso"]["tp"], dp["container-ovsdpdk"]["tp"],
      dp["bare-tas"]["std"], dp["virt-tas"]["std"],
      dp["ovs-tas"]["std"], dp["bare-linux"]["std"], dp["ovs-linux"]["std"],
      dp["container-tas"]["std"], dp["container-virtuoso"]["std"], dp["container-ovsdpdk"]["std"],))

def main():
  parsed_md = parse_metadata()
  latencies = parse_data(parsed_md)
  save_dat_file(latencies, "./tp.dat")

if __name__ == '__main__':
  main()
