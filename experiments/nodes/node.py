import time

class Node:
  
  def __init__(self, defaults, machine_config, cset_configs,
      wmanager, setup_pane_name, cleanup_pane_name):
    self.defaults = defaults
    self.cset_configs = {}
    for cset_config in cset_configs:
      self.cset_configs[cset_config.name] = cset_config
    self.machine_config = machine_config
    self.wmanager = wmanager
    self.setup_pane_name = setup_pane_name
    self.cleanup_pane_name = cleanup_pane_name

  def setup(self):
    self.setup_pane = self.wmanager.add_new_pane(self.setup_pane_name, 
        self.machine_config.is_remote)

    # We disabled cset for now because it doesn't work with cgroups v2    
    # for cset in self.cset_configs:
    #     self.set_cset(cset.cores_arg, cset.mem, cset.name, cset.exclusive)

    self.setup_pane.send_keys("sudo sysctl -w net.ipv4.tcp_tw_reuse=1")
    time.sleep(1)
    self.setup_pane.send_keys("sudo sysctl -w net.ipv4.tcp_fin_timeout=1")
    time.sleep(1)
    # self.setup_pane.send_keys('sudo sysctl -w net.ipv4.ip_local_port_range="1024 65535"')

  def cleanup(self):
    self.cleanup_pane = self.wmanager.add_new_pane(self.cleanup_pane_name, 
        self.machine_config.is_remote)
    
    # We disabled cset for now because it doesn't work with cgroups v2  
    # for cset in self.cset_configs:
    #   self.destroy_cset(cset.name)

  def remove_file(self, pane, filename):
     cmd = "rm {}".format(filename)
     pane.send_keys(cmd)

  def add_ip_in_pane(self, pane, interface, ip):
    cmd = "sudo ip addr add {} dev {}".format(ip, interface)
    pane.send_keys(cmd)
    time.sleep(1)

  def interface_up_in_pane(self, pane, interface):
    cmd = "sudo ip link set dev {} up".format(interface)
    pane.send_keys(cmd)
    time.sleep(1)

  def interface_del_in_pane(self, pane, interface):
    cmd = "sudo ip link delete {}".format(interface)
    pane.send_keys(cmd)
    time.sleep(1)

  def interface_del(self, interface):
    self.interface_del_in_pane(self.cleanup_pane, interface)

  def set_cset(self, cores_arg, mem, name, exclusive):
    if exclusive:
        cmd = "sudo cset set --cpu={} --mem={} --set={} --cpu_exclusive".format(cores_arg, mem, name)
    else:
        cmd = "sudo cset set --cpu={} --mem={} --set={}".format(cores_arg, mem, name)

    self.setup_pane.send_keys(cmd)
    time.sleep(2)

  def destroy_cset(self, name):
     cmd = "sudo cset set --destroy --set={}".format(name)
     self.cleanup_pane.send_keys(cmd)
     time.sleep(2)

  def add_ip(self, interface, ip):
    cmd = "sudo ip addr add {} dev {}".format(ip, interface)
    self.setup_pane.send_keys(cmd)
    time.sleep(1)

  def interface_up(self, interface):
    cmd = "sudo ip link set dev {} up".format(interface)
    self.setup_pane.send_keys(cmd)
    time.sleep(1)

  def iptables_f(self):
    cmd = "sudo iptables -F"
    self.setup_pane.send_keys(cmd)
    time.sleep(1)

  def tap_up(self, interface, script_dir, multi_queue):
    cmd = "cd {}".format(script_dir)
    self.setup_pane.send_keys(cmd)
    time.sleep(1)
    cmd = "sudo bash tap-up.sh {} {}".format(interface, multi_queue)
    self.setup_pane.send_keys(cmd)
    time.sleep(2)

  def tap_down(self, interface, script_dir):
    cmd = "cd {}".format(script_dir)
    self.cleanup_pane.send_keys(cmd)
    time.sleep(1)
    cmd = "sudo bash tap-down.sh {}".format(interface)
    self.cleanup_pane.send_keys(cmd)
    time.sleep(2)

  # Args are the ip for the interface TAS normally used
  # and its ip
  def bridge_up(self, ip, interface, script_dir):
      cmd = "cd {}".format(script_dir)
      self.setup_pane.send_keys(cmd)
      time.sleep(1)
      cmd = "sudo bash br-up.sh {} {}/24".format(interface, ip)
      self.setup_pane.send_keys(cmd)
      time.sleep(2)
  
  # Args are the ip for the interface TAS normally used
  # and its ip
  def bridge_down(self, ip, interface, script_dir):
      cmd = "cd {}".format(script_dir)
      self.cleanup_pane.send_keys(cmd)
      time.sleep(1)
      cmd = "sudo bash br-down.sh {} {}/24".format(interface, ip)
      self.cleanup_pane.send_keys(cmd)
      time.sleep(2)

  def ovs_make_uninstall(self, ovs_mod_dir, ovs_o_dir):
      cmd = "cd {}".format(ovs_o_dir)
      self.setup_pane.send_keys(cmd)
      time.sleep(1)
      cmd = "sudo make uninstall"
      self.setup_pane.send_keys(cmd)
      time.sleep(1)

      cmd = "cd {}".format(ovs_mod_dir)
      self.setup_pane.send_keys(cmd)
      time.sleep(1)
      cmd = "sudo make uninstall"
      self.setup_pane.send_keys(cmd)
      time.sleep(1)

  def ovs_make_install(self, ovs_dir):
      cmd = "cd {}".format(ovs_dir)
      self.setup_pane.send_keys(cmd)
      time.sleep(1)
      cmd = "sudo make install"
      self.setup_pane.send_keys(cmd)
      time.sleep(3)

  def start_ovs(self, script_dir):
      cmd = "cd {}".format(script_dir)
      self.setup_pane.send_keys(cmd)
      time.sleep(1)
      cmd = "sudo bash ovs-start.sh"
      self.setup_pane.send_keys(cmd)
      time.sleep(4)

  def stop_ovs(self, script_dir):
      cmd = "cd {}".format(script_dir)
      self.cleanup_pane.send_keys(cmd)
      time.sleep(1)
      cmd = "sudo bash ovs-stop.sh"
      self.cleanup_pane.send_keys(cmd)
      time.sleep(2)

  def start_ovsdpdk(self, pmd_mask, script_dir):
      cmd = "cd {}".format(script_dir)
      self.setup_pane.send_keys(cmd)
      time.sleep(1)
      cmd = "sudo bash ovsdpdk-start.sh {}".format(pmd_mask)
      self.setup_pane.send_keys(cmd)
      time.sleep(4)

  def stop_ovsdpdk(self, script_dir):
      cmd = "cd {}".format(script_dir)
      self.cleanup_pane.send_keys(cmd)
      time.sleep(1)
      cmd = "sudo bash ovsdpdk-stop.sh"
      self.cleanup_pane.send_keys(cmd)
      time.sleep(2)

  def ovs_route_add(self, br_name, ip):
     cmd = "sudo ovs-appctl ovs/route/add {} {}".format(ip, br_name)
     self.setup_pane.send_keys(cmd)
     time.sleep(1)

  def ovsbr_add(self, br_name, ip, interface, script_dir):
      cmd = "cd {}".format(script_dir)
      self.setup_pane.send_keys(cmd)
      time.sleep(1)
      cmd = "sudo bash ovsbr-add.sh {} {} {}".format(br_name, ip, interface)
      self.setup_pane.send_keys(cmd)
      time.sleep(4)

  def ovsbr_add_internal(self, br_name, script_dir):
      cmd = "cd {}".format(script_dir)
      self.setup_pane.send_keys(cmd)
      time.sleep(1)
      cmd = "sudo bash ovsbr-add-int.sh {}".format(br_name)
      self.setup_pane.send_keys(cmd)
      time.sleep(2)

  def ovsbr_add_physical(self, br_name, mac, script_dir):
      cmd = "cd {}".format(script_dir)
      self.setup_pane.send_keys(cmd)
      time.sleep(1)
      cmd = "sudo bash ovsbr-add-phy.sh {} {}".format(br_name, mac)
      self.setup_pane.send_keys(cmd)
      time.sleep(2)

  def ovsbr_add_vtuoso(self, br_name, script_dir):
      cmd = "cd {}".format(script_dir)
      self.setup_pane.send_keys(cmd)
      time.sleep(1)
      cmd = "sudo bash ovs-add-vtuosobr.sh {}".format(br_name)
      self.setup_pane.send_keys(cmd)
      time.sleep(4)

  def ovsbr_del(self, br_name):
      cmd = "sudo ovs-vsctl del-br {}".format(br_name)
      self.cleanup_pane.send_keys(cmd)
      time.sleep(2)
  
  def set_dpdk_interface(self, interface, pci_id, n_queues):
      cmd = "sudo ovs-vsctl set Interface {} ".format(interface) + \
        "type=dpdk options:n_rxq={} options:dpdk-devargs={}".format(n_queues, pci_id)
      self.setup_pane.send_keys(cmd)
     
  def ovsvhost_add(self, br_name, vhost_name, 
                   n_queues, script_dir):
      cmd = "cd {}".format(script_dir)
      self.setup_pane.send_keys(cmd)
      time.sleep(1)
      cmd = "sudo bash ovsvhost-add.sh {} {} {}".format(
          br_name, vhost_name, n_queues)
      self.setup_pane.send_keys(cmd)
      time.sleep(4)

  def ovsbr_add_port(self, br_name, port_name):
     cmd = "sudo ovs-vsctl add-port {} {}".format(br_name, port_name)
     self.setup_pane.send_keys(cmd)
     time.sleep(1)

  def ovsport_add_vtuoso(self, br_name, port_name, port_type, vmid, script_dir,
                         out_remote_ip=None, out_local_ip=None,
                         in_remote_ip=None, in_local_ip=None, key=None):
      cmd = "cd {}".format(script_dir)
      self.setup_pane.send_keys(cmd)
      time.sleep(1)
      if port_type == "virtuosotx":
        cmd = "sudo bash ovs-add-vtuosoport.sh {} {} {} {} {} {} {} {} {}".format(
            br_name, port_name, port_type, vmid,
            out_remote_ip, out_local_ip, in_remote_ip, in_local_ip, key) 
      else:
        cmd = "sudo bash ovs-add-vtuosoport.sh {} {} {} {}".format(
            br_name, port_name, port_type, vmid)
      self.setup_pane.send_keys(cmd)
      time.sleep(2)

  def ovstunnel_add(self, br_name, tun_name, remote_ip, script_dir, key=None):
      cmd = "cd {}".format(script_dir)
      self.setup_pane.send_keys(cmd)
      time.sleep(1)
      if key is None:
        cmd = "sudo bash ovs-add-tunnel.sh {} {} {}".format(
            br_name, tun_name, remote_ip)
      else:
         cmd = "sudo bash ovs-add-tunnel.sh {} {} {} {}".format(
            br_name, tun_name, remote_ip, key)
      self.setup_pane.send_keys(cmd)
      time.sleep(2)    

  def ovstap_add(self, br_name, tap_name, multi_queue, script_dir):
      cmd = "cd {}".format(script_dir)
      self.setup_pane.send_keys(cmd)
      time.sleep(1)
      cmd = "sudo bash ovstap-add.sh {} {} {}".format(
          br_name, tap_name, multi_queue)
      self.setup_pane.send_keys(cmd)
      time.sleep(2)

  def ovstap_del(self, script_dir):
      cmd = "cd {}".format(script_dir)
      self.cleanup_pane.send_keys(cmd)
      time.sleep(1)

  def ovsflow_add(self, br_name, in_port, out_port, script_dir):
      cmd = "cd {}".format(script_dir)
      self.setup_pane.send_keys(cmd)
      time.sleep(1)
      cmd = "sudo bash ovsflow-add.sh {} {} {}".format(br_name, in_port, out_port)
      self.setup_pane.send_keys(cmd)
      time.sleep(2)

  def ovsveth_add(self, br_name, veth_name_bridge, 
                  veth_name_container, script_dir, 
                  container_name, veth_bridge_ip,                                       
                  veth_container_ip, n_queues=1):
      
      cmd = "cd {}".format(script_dir)
      self.setup_pane.send_keys(cmd)
      time.sleep(1)
      cmd = "sudo bash ovsveth-add.sh {} {} {} {} {} {} {}".format(
            br_name, veth_name_bridge, 
            veth_name_container, n_queues, 
            container_name, veth_bridge_ip, 
            veth_container_ip)
      self.setup_pane.send_keys(cmd)
      time.sleep(1)