import sys
sys.path.append("../../../")

import os
import numpy as np
import experiments.plot_utils as putils
import bisect

TSC_DIV = 100000

def check_overlap(data, overlap):
  if overlap not in data:
    data[overlap] = {}

def check_fpcores(data, overlap, fpcores):
  if fpcores not in data[overlap]:
    data[overlap][fpcores] = {}

def check_stack(data, overlap, fpcores, stack):
  if stack not in data[overlap][fpcores]:
    data[overlap][fpcores][stack] = {}

def check_run(data, overlap, fpcores, stack, run):
  if run not in data[overlap][fpcores][stack]:
    data[overlap][fpcores][stack][run] = {}

def check_nid(data, overlap, fpcores, stack, run, nid):
  if nid not in data[overlap][fpcores][stack][run]:
    data[overlap][fpcores][stack][run][nid] = {}

def check_cid(data, overlap, fpcores, stack, run, nid, cid):
  if cid not in data[overlap][fpcores][stack][run][nid]:
    data[overlap][fpcores][stack][run][nid][cid] = ""

def parse_metadata():
  dir_path = "./out/"
  data = {}

  for f in os.listdir(dir_path):
    fname = os.fsdecode(f)

    if "tas_c" == fname or "latency_hist" in fname:
      continue

    run = putils.get_expname_run(fname)
    overlap = putils.get_expname_overlap(fname)
    fpcores = putils.get_expname_fpcores(fname)
    cid = putils.get_client_id(fname)
    nid = putils.get_node_id(fname)
    stack = putils.get_stack(fname)

    check_overlap(data, overlap)
    check_fpcores(data, overlap, fpcores)
    check_stack(data, overlap, fpcores, stack)
    check_run(data, overlap, fpcores, stack, run)
    check_nid(data, overlap, fpcores, stack, run, nid)
    check_cid(data, overlap, fpcores, stack, run, nid, cid)

    data[overlap][fpcores][stack][run][nid][cid] = fname

  return data

def parse_data(parsed_md):
  data = {}
  out_dir = "./out/"
  for overlap in parsed_md:
    for fpcores in parsed_md[overlap]:
      for stack in parsed_md[overlap][fpcores]:
        for run in parsed_md[overlap][fpcores][stack]:

          c0_fname = out_dir + parsed_md[overlap][fpcores][stack][run]["0"]["0"]
          c1_fname = out_dir + parsed_md[overlap][fpcores][stack][run]["0"]["1"]
          c2_fname = out_dir + parsed_md[overlap][fpcores][stack][run]["0"]["2"]
          c3_fname = out_dir + parsed_md[overlap][fpcores][stack][run]["0"]["3"]

          timestamps = get_timestamps(c0_fname)
          tps = get_throughput(c0_fname)
          c0_seconds = np.linspace(start=0, 
                                  stop=len(timestamps), 
                                  num=len(timestamps),
                                  endpoint=False).astype(int)
          assert(len(timestamps) == len(c0_seconds))
          assert(len(tps) == len(c0_seconds))

          c1_seconds = timestamps_to_seconds(timestamps, c1_fname)
          c2_seconds = timestamps_to_seconds(timestamps, c2_fname)
          c3_seconds = timestamps_to_seconds(timestamps, c3_fname)
          count_tp(tps, c0_seconds, c1_seconds, c1_fname)
          count_tp(tps, c0_seconds, c2_seconds, c2_fname)
          count_tp(tps, c0_seconds, c3_seconds, c3_fname)


          if stack not in data:
            data[stack] = {}

          if overlap not in data[stack]:
            data[stack][overlap] = {}

          if fpcores not in data[stack][overlap]:
            data[stack][overlap][fpcores] = {}
            
          data[stack][overlap][fpcores]["tp"] = tps
          data[stack][overlap][fpcores]["ts"] = c0_seconds

  return data

def get_throughput(fname):
  tps = []
  f = open(fname)
  lines = f.readlines()

  for i, line in enumerate(lines):
    if i == 0:
      tp = float(putils.get_n_messages(line))
    else:
      tp = float(putils.get_n_messages(line)) - float(putils.get_n_messages(lines[i - 1]))

    tps.append(tp)
  return tps

def count_tp(tps, base_secs, secs, fname):
  f = open(fname)
  lines = f.readlines()
  end_sec = base_secs[len(base_secs) - 1]

  for i in range(len(lines)):
    line = lines[i]
    sec = secs[i]

    if sec > end_sec:
      break

    if i == 0:
      tp = float(putils.get_n_messages(line))
    else:
      tp = float(putils.get_n_messages(line)) - float(putils.get_n_messages(lines[i - 1]))

    tps[sec] += tp

def timestamps_to_seconds(timestamps, fname):
  f = open(fname)
  lines = f.readlines()
  first_ts = int(putils.get_ts(lines[0]))
  first_second = bisect.bisect(timestamps, first_ts)
  seconds = np.linspace(start=first_second,
                        stop=first_second + len(lines),
                        num=len(lines)).astype(int)
  assert(len(seconds) == len(lines))
  return seconds

def get_timestamps(fname):
  f = open(fname)
  lines = f.readlines()
  
  intervals = []

  for line in lines:
    tsc = int(putils.get_ts(line))
    intervals.append(tsc)
  
  return intervals

def add_timestamps(fname, ts_l, ts_l_idx, client):
  f = open(fname)
  lines = f.readlines()

  for line in lines:
    tsc = int(putils.get_ts(line))
    ts_l.append(tsc)
    ts_l_idx.append({"tsc": tsc, "id": client})

def add_to_ts_list(ts_l, ts_l_idx, fname):
  f = open(fname)
  lines = f.readlines()
  for i in range(len(lines)):
    line = lines[i]
    tsc = int(int(putils.get_ts(line)) / TSC_DIV)
    ts_l.append(tsc)
    ts_l_idx.append({"tsc": tsc, "i": i})

def populate_frequency_bins(bins, intervals, fname):
  f = open(fname)
  lines = f.readlines()
  for line in lines:
    tsc = int(putils.get_ts(line))  
    idx = bisect.bisect(intervals, tsc)
    if idx < len(intervals):
      bins[idx] += float(putils.get_tp(line).replace(",", ""))

def get_burst_average(n_bursts, burst_length, burst_interval, data):
  start_idx = burst_interval
  end_idx = start_idx + burst_length
  avg = 0
  for i in range(n_bursts):
    avg += np.mean(data["tp"][start_idx:end_idx - 1])
    start_idx = end_idx + burst_interval
    end_idx = start_idx + burst_length

  avg /= n_bursts
  return avg

def main():
  n_bursts = 3
  burst_length = 10
  burst_interval = 50
  overlaps = ["25", "50", "75", "100"]
  fpcore = "3"
  parsed_md = parse_metadata()
  data = parse_data(parsed_md)

  fname_virtuoso = "./resoeff-bar-tp.dat"
  f_virtuoso = open(fname_virtuoso, "w+")
  header = "overlap virtuoso-tp ovstas-tp\n"
  f_virtuoso.write(header)

  for overlap in overlaps:
      virtuoso_avg = get_burst_average(n_bursts, burst_length, burst_interval, 
                              data["virt-tas"][overlap][fpcore])
      virtuoso_avg /= int(fpcore)

      ovstas_avg = get_burst_average(n_bursts, burst_length, burst_interval, 
                              data["ovs-tas"][overlap]["1"])
      ovstas_avg /= 4

      f_virtuoso.write("{} {} {}\n".format(overlap, virtuoso_avg, ovstas_avg))

if __name__ == '__main__':
  main()