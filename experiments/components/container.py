import time


class Container:
    def __init__(self, defaults, machine_config, container_config, wmanager):
        self.defaults = defaults
        self.machine_config = machine_config
        self.container_config = container_config
        self.wmanager = wmanager
        self.pane = self.wmanager.add_new_pane(container_config.pane,
                                               machine_config.is_remote)

    def start(self):
        self.pane.send_keys('cd ' + self.container_config.manager_dir)
        start_container_cmd = "sudo bash start-container.sh {} {} {} {} {} {} {}".format(
            self.machine_config.stack, self.container_config.id,
            self.container_config.n_cores, self.container_config.memory,
            self.container_config.name, self.container_config.tas_dir,
            self.container_config.cset)
        self.pane.send_keys(start_container_cmd)

        print("Started Container")
        time.sleep(3)

        self.pane.send_keys("sudo docker start {}".format(
            self.container_config.name))
        time.sleep(1)
        if self.container_config.tunnel:
            self.add_dummy_intf(
                "eth0", self.container_config.veth_container_ip, "C8:D7:4A:4E:47:50")
        self.enter_container()
        self.pane.send_keys("sudo sysctl -w net.ipv4.tcp_tw_reuse=1")
        time.sleep(1)
        self.pane.send_keys("sudo sysctl -w net.ipv4.tcp_fin_timeout=1")
        time.sleep(1)


    def enter_container(self):
        enter_container_cmd = "sudo docker exec -it {} bash".format(
            self.container_config.name)
        self.pane.send_keys(enter_container_cmd)
        time.sleep(1)

    def add_dummy_intf(self, interface, ip, mac):
        cmd = 'cd ' + self.container_config.manager_dir
        self.pane.send_keys(cmd)
        cmd = "bash container-dummy-intf-add.sh {} {} {} {}".format(
            interface, ip, mac, self.container_config.name)
        self.pane.send_keys(cmd)
        time.sleep(1)

    def shutdown(self):
        pass
        # self.pane.send_keys(suppress_history=False, cmd='whoami')
        # time.sleep(1)

        # captured_pane = self.pane.capture_pane()
        # user = captured_pane[len(captured_pane) - 2]

        # # This means we are in the container, so we don't
        # # accidentally exit machine
        # if user == 'tas':
        #     self.pane.send_keys(suppress_history=False, cmd='exit')
        #     time.sleep(3)

        # kill_container_cmd = "sudo docker stop {}".format(
        #     self.container_config.name)
        # self.pane.send_keys(kill_container_cmd)
        # time.sleep(10)
        # kill_container_cmd = "sudo docker remove {}".format(
        #     self.container_config.name)
        # time.sleep(1)
        # prune_container_cmd = "sudo docker container prune -f"
        # self.pane.send_keys(prune_container_cmd)
        # time.sleep(2)