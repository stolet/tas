import time
import os

import utils

class Client:
    
    def __init__(self, defaults, machine_config, client_config, vm_config, cset_configs, wmanager):
        self.defaults = defaults
        self.machine_config = machine_config
        self.client_config = client_config
        self.vm_config = vm_config
        self.cset_configs = cset_configs
        self.wmanager = wmanager
        self.pane = self.wmanager.add_new_pane(client_config.pane, 
                machine_config.is_remote)
        self.save_logs_pane = self.wmanager.add_new_pane(defaults.c_savelogs_pane,
                machine_config.is_remote)

    def run_bare(self, w_sudo, ld_preload):
        self.run_benchmark_rpc(w_sudo, ld_preload, clean=False, cset=True)

    def run_virt(self, w_sudo, ld_preload):
        ssh_com = utils.get_ssh_command(self.machine_config, self.vm_config)
        self.pane.send_keys(ssh_com)
        time.sleep(3)
        self.pane.send_keys("tas")
        time.sleep(2)
        self.run_benchmark_rpc(w_sudo, ld_preload, clean=False, cset=False)

    def run_benchmark_rpc(self, w_sudo, ld_preload, clean, cset):
        self.pane.send_keys('cd ' + self.client_config.comp_dir)

        if clean:
            self.pane.send_keys(self.client_config.clean_cmd)
            time.sleep(1)

        self.pane.send_keys(self.client_config.comp_cmd)
        time.sleep(1)
        self.pane.send_keys("cd " + self.client_config.tas_dir)
        time.sleep(3)

        cmd = ''
        
        if w_sudo:
            cmd += 'sudo '

        if ld_preload:
            cmd += 'LD_PRELOAD=' + self.client_config.lib_so + ' '

        if cset:
            cmd += "taskset -c {} ".format(self.cset_configs[self.client_config.cset].cores_arg)
            cmd += self.client_config.exec_file + ' '
            cmd += self.client_config.args + ' | tee ' + self.client_config.out
        else:
            cmd += self.client_config.exec_file + ' ' + \
                    self.client_config.args + \
                    ' | tee ' + \
                    self.client_config.out
    
        self.pane.send_keys(cmd)
    
    def save_log_virt(self, exp_path):
        split_path = exp_path.split("/")
        n = len(split_path)
        
        out_dir = os.getcwd() + "/" + "/".join(split_path[:n - 1]) + "/out"
        if not os.path.exists(out_dir):
            os.makedirs(out_dir)

        scp_com = utils.get_scp_command(self.machine_config, self.vm_config,
                self.client_config.out,
                out_dir + '/' + self.client_config.out_file)
        self.save_logs_pane.send_keys(scp_com)
        time.sleep(3)
        self.save_logs_pane.send_keys(suppress_history=False, cmd='tas')
        time.sleep(1)

        if self.client_config.hist_out is not None:
            scp_com = utils.get_scp_command(self.machine_config, self.vm_config,
                self.client_config.hist_out,
                out_dir + '/' + self.client_config.hist_file)
            self.save_logs_pane.send_keys(scp_com)
            time.sleep(3)
            self.save_logs_pane.send_keys(suppress_history=False, cmd='tas')
            time.sleep(1)

        if self.client_config.hist_msgs_out is not None:
            scp_com = utils.get_scp_command(self.machine_config, self.vm_config,
                self.client_config.hist_msgs_out,
                out_dir + '/' + self.client_config.hist_msgs_file)
            self.save_logs_pane.send_keys(scp_com)
            time.sleep(3)
            self.save_logs_pane.send_keys(suppress_history=False, cmd='tas')
            time.sleep(1)

        if self.client_config.hist_open_out is not None:
            scp_com = utils.get_scp_command(self.machine_config, self.vm_config,
                self.client_config.hist_open_out,
                out_dir + '/' + self.client_config.hist_open_file)
            self.save_logs_pane.send_keys(scp_com)
            time.sleep(3)
            self.save_logs_pane.send_keys(suppress_history=False, cmd='tas')
            time.sleep(1)

        # Remove log from remote machine
        ssh_com = utils.get_ssh_command(self.machine_config, self.vm_config)
 
        ssh_com += " 'rm {}'".format(self.client_config.out)
        self.save_logs_pane.send_keys(ssh_com)
        time.sleep(3)
        self.save_logs_pane.send_keys(suppress_history=False, cmd='tas')
        time.sleep(1)

        if self.client_config.hist_out is not None and self.client_config.conn_latency:
            ssh_com += " 'rm {}'".format(self.client_config.hist_out)
            self.save_logs_pane.send_keys(ssh_com)
            time.sleep(3)
            self.save_logs_pane.send_keys(suppress_history=False, cmd='tas')
            time.sleep(1)

        if self.client_config.hist_msgs_out is not None and self.client_config.conn_latency:
            ssh_com += " 'rm {}'".format(self.client_config.hist_msgs_out)
            self.save_logs_pane.send_keys(ssh_com)
            time.sleep(3)
            self.save_logs_pane.send_keys(suppress_history=False, cmd='tas')
            time.sleep(1)

        if self.client_config.hist_open_out is not None and self.client_config.conn_latency:
            ssh_com += " 'rm {}'".format(self.client_config.hist_open_out)
            self.save_logs_pane.send_keys(ssh_com)
            time.sleep(3)
            self.save_logs_pane.send_keys(suppress_history=False, cmd='tas')
            time.sleep(1)

    def save_log_bare(self, exp_path):
        # self.exp_path is set in the run.py file
        split_path = exp_path.split("/")
        n = len(split_path)
        
        out_dir = os.getcwd() + "/" + "/".join(split_path[:n - 1]) + "/out"
        if not os.path.exists(out_dir):
            os.makedirs(out_dir)

        dest = out_dir + "/" + self.client_config.out_file
        os.rename(self.client_config.out, dest)

        if self.client_config.hist_file is not None and self.client_config.conn_latency:
            dest_hist = out_dir + "/" + self.client_config.hist_file
            os.rename(self.client_config.hist_out, dest_hist)

        if self.client_config.hist_msgs_file is not None and self.client_config.conn_latency:
            dest_hist = out_dir + "/" + self.client_config.hist_msgs_file
            os.rename(self.client_config.hist_msgs_out, dest_hist)

        if self.client_config.hist_open_file is not None and self.client_config.conn_latency:
            dest_hist = out_dir + "/" + self.client_config.hist_open_file
            os.rename(self.client_config.hist_open_out, dest_hist)