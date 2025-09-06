# Copyright (c) 2025 Zhanshuo Tang & Haitian Qian, AI+X
# No rights reserved.


from m5.params import *
from m5.objects import *

from common import FileSystemConfig

from topologies.BaseTopology import SimpleTopology

# Creates a generic Mesh assuming an equal number of cache
# and directory controllers.
# XY routing is enforced (using link weights)
# to guarantee deadlock freedom.


class SimpleCubic(SimpleTopology):
    description = "SimpleCubic"

    def __init__(self, controllers):
        self.nodes = controllers

    # Makes a generic mesh
    # assuming an equal number of cache and directory cntrls

    def makeTopology(self, options, network, IntLink, ExtLink, Router):
        nodes = self.nodes

        num_routers = options.num_cpus
        num_rows = options.num_rows
        num_columns = options.num_cols

        # default values for link latency and router latency.
        # Can be over-ridden on a per link/router basis
        link_latency = options.link_latency  # used by simple and garnet
        router_latency = options.router_latency  # only used by garnet

        # There must be an evenly divisible number of cntrls to routers
        # Also, obviously the number or rows must be <= the number of routers
        cntrls_per_router, remainder = divmod(len(nodes), num_routers)

        # Create the routers in the mesh
        routers = [
            Router(router_id=i, latency=router_latency)
            for i in range(num_routers)
        ]
        network.routers = routers
        num_layers = int(num_routers / (num_rows * num_columns))
        assert num_layers * num_columns * num_rows == num_routers
        assert num_layers % 2 == 0
        
        # link counter to set unique link ids
        link_count = 0

        # Add all but the remainder nodes to the list of nodes to be uniformly
        # distributed across the network.
        network_nodes = []
        remainder_nodes = []
        for node_index in range(len(nodes)):
            if node_index < (len(nodes) - remainder):
                network_nodes.append(nodes[node_index])
            else:
                remainder_nodes.append(nodes[node_index])

        # Connect each node to the appropriate router
        ext_links = []
        for (i, n) in enumerate(network_nodes):
            cntrl_level, router_id = divmod(i, num_routers)
            assert cntrl_level < cntrls_per_router
            ext_links.append(
                ExtLink(
                    link_id=link_count,
                    ext_node=n,
                    int_node=routers[router_id],
                    latency=link_latency,
                )
            )
            link_count += 1

        # Connect the remainding nodes to router 0.  These should only be
        # DMA nodes.
        for (i, node) in enumerate(remainder_nodes):
            assert node.type == "DMA_Controller"
            assert i < remainder
            ext_links.append(
                ExtLink(
                    link_id=link_count,
                    ext_node=node,
                    int_node=routers[0],
                    latency=link_latency,
                )
            )
            link_count += 1

        network.ext_links = ext_links

        # Create the mesh links.
        int_links = []
        def Pos2Idx(pos):
            x, y, z = pos
            x = x % num_columns
            y = y % num_rows
            z = z % num_layers
            return int(z * (num_rows * num_columns) + y * num_columns + x)

        for z in range(num_layers):
            for x in range(num_columns):
                for y in range(num_rows):
                    current_pos = (x, y, z)

                    def add_link(pos, src_outport, dst_inport, weight):
                        nonlocal link_count
                        # print(f"Adding link from {pos} to {current_pos} with weight {weight}")
                        # print(f"Routing from {Pos2Idx(pos)} to {Pos2Idx(current_pos)}")
                        int_links.append(
                            IntLink(
                                link_id=link_count,
                                src_node=routers[Pos2Idx(pos)],
                                dst_node=routers[Pos2Idx(current_pos)],
                                src_outport=src_outport,
                                dst_inport=dst_inport,
                                latency=link_latency,
                                weight=weight,
                            )
                        )
                        # print(f"Link added: {link_count}")
                        link_count += 1

                    left_pos = (x-1, y, z)
                    right_pos = (x+1, y, z)
                    down_pos = (x, y-1, z)
                    up_pos = (x, y+1, z)
                    back_pos = (x, y, z-1)
                    front_pos = (x, y, z+1)


                    add_link(left_pos, '+00', '-00', 2)
                    add_link(right_pos, '-00', '+00', 2)
                    add_link(down_pos, '0+0', '0-0', 3)
                    add_link(up_pos, '0-0', '0+0', 3)
                    add_link(back_pos, '00+', '00-', 1)
                    add_link(front_pos, '00-', '00+', 1)


        network.int_links = int_links

    # Register nodes with filesystem
    def registerTopology(self, options):
        for i in range(options.num_cpus):
            FileSystemConfig.register_node(
                [i], MemorySize(options.mem_size) // options.num_cpus, i
            )
