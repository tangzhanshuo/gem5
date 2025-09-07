/*
 * Copyright (c) 2008 Princeton University
 * Copyright (c) 2016 Georgia Institute of Technology
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#include "mem/ruby/network/garnet/RoutingUnit.hh"

#include "base/cast.hh"
#include "base/compiler.hh"
#include "debug/RubyNetwork.hh"
#include "mem/ruby/network/garnet/InputUnit.hh"
#include "mem/ruby/network/garnet/Router.hh"
#include "mem/ruby/slicc_interface/Message.hh"

#include <random>

namespace gem5
{

namespace ruby
{

namespace garnet
{

RoutingUnit::RoutingUnit(Router *router)
{
    m_router = router;
    m_routing_table.clear();
    m_weight_table.clear();
}

void
RoutingUnit::addRoute(std::vector<NetDest>& routing_table_entry)
{
    if (routing_table_entry.size() > m_routing_table.size()) {
        m_routing_table.resize(routing_table_entry.size());
    }
    for (int v = 0; v < routing_table_entry.size(); v++) {
        m_routing_table[v].push_back(routing_table_entry[v]);
    }
}

void
RoutingUnit::addWeight(int link_weight)
{
    m_weight_table.push_back(link_weight);
}

bool
RoutingUnit::supportsVnet(int vnet, std::vector<int> sVnets)
{
    // If all vnets are supported, return true
    if (sVnets.size() == 0) {
        return true;
    }

    // Find the vnet in the vector, return true
    if (std::find(sVnets.begin(), sVnets.end(), vnet) != sVnets.end()) {
        return true;
    }

    // Not supported vnet
    return false;
}

/*
 * This is the default routing algorithm in garnet.
 * The routing table is populated during topology creation.
 * Routes can be biased via weight assignments in the topology file.
 * Correct weight assignments are critical to provide deadlock avoidance.
 */
int
RoutingUnit::lookupRoutingTable(int vnet, NetDest msg_destination)
{
    // First find all possible output link candidates
    // For ordered vnet, just choose the first
    // (to make sure different packets don't choose different routes)
    // For unordered vnet, randomly choose any of the links
    // To have a strict ordering between links, they should be given
    // different weights in the topology file

    int output_link = -1;
    int min_weight = INFINITE_;
    std::vector<int> output_link_candidates;
    int num_candidates = 0;

    // Identify the minimum weight among the candidate output links
    for (int link = 0; link < m_routing_table[vnet].size(); link++) {
        if (msg_destination.intersectionIsNotEmpty(
            m_routing_table[vnet][link])) {

        if (m_weight_table[link] <= min_weight)
            min_weight = m_weight_table[link];
        }
    }

    // Collect all candidate output links with this minimum weight
    for (int link = 0; link < m_routing_table[vnet].size(); link++) {
        if (msg_destination.intersectionIsNotEmpty(
            m_routing_table[vnet][link])) {

            if (m_weight_table[link] == min_weight) {
                num_candidates++;
                output_link_candidates.push_back(link);
            }
        }
    }

    if (output_link_candidates.size() == 0) {
        fatal("Fatal Error:: No Route exists from this Router.");
        exit(0);
    }

    // Randomly select any candidate output link
    int candidate = 0;
    if (!(m_router->get_net_ptr())->isVNetOrdered(vnet))
        candidate = rand() % num_candidates;

    output_link = output_link_candidates.at(candidate);
    return output_link;
}


void
RoutingUnit::addInDirection(PortDirection inport_dirn, int inport_idx)
{
    m_inports_dirn2idx[inport_dirn] = inport_idx;
    m_inports_idx2dirn[inport_idx]  = inport_dirn;
}

void
RoutingUnit::addOutDirection(PortDirection outport_dirn, int outport_idx)
{
    m_outports_dirn2idx[outport_dirn] = outport_idx;
    m_outports_idx2dirn[outport_idx]  = outport_dirn;
}

// outportCompute() is called by the InputUnit
// It calls the routing table by default.
// A template for adaptive topology-specific routing algorithm
// implementations using port directions rather than a static routing
// table is provided here.

int
RoutingUnit::outportCompute(RouteInfo route, int inport,
                            PortDirection inport_dirn)
{
    int outport = -1;

    if (route.dest_router == m_router->get_id()) {

        // Multiple NIs may be connected to this router,
        // all with output port direction = "Local"
        // Get exact outport id from table
        outport = lookupRoutingTable(route.vnet, route.net_dest);
        return outport;
    }

    // Routing Algorithm set in GarnetNetwork.py
    // Can be over-ridden from command line using --routing-algorithm = 1
    RoutingAlgorithm routing_algorithm =
        (RoutingAlgorithm) m_router->get_net_ptr()->getRoutingAlgorithm();

    switch (routing_algorithm) {
        case TABLE_:  outport =
            lookupRoutingTable(route.vnet, route.net_dest); break;
        case XY_:     outport =
            outportComputeXY(route, inport, inport_dirn); break;
        // any custom algorithm
        case ZXY_: outport =
            outportComputeZXY(route, inport, inport_dirn); break;
        case FCC_DETERMINISTIC_: outport =
            outportComputeFCC(route, inport, inport_dirn, false); break;
        case FCC_RANDOM_: outport =
            outportComputeFCC(route, inport, inport_dirn, true); break;
        case BCC_DETERMINISTIC_: outport =
            outportComputeBCC(route, inport, inport_dirn, false); break;
        case BCC_RANDOM_: outport =
            outportComputeBCC(route, inport, inport_dirn, true); break;
        default: outport =
            lookupRoutingTable(route.vnet, route.net_dest); break;
    }

    assert(outport != -1);
    return outport;
}

// XY routing implemented using port directions
// Only for reference purpose in a Mesh
// By default Garnet uses the routing table
int
RoutingUnit::outportComputeXY(RouteInfo route,
                              int inport,
                              PortDirection inport_dirn)
{
    PortDirection outport_dirn = "Unknown";

    [[maybe_unused]] int num_rows = m_router->get_net_ptr()->getNumRows();
    int num_cols = m_router->get_net_ptr()->getNumCols();
    assert(num_rows > 0 && num_cols > 0);

    int my_id = m_router->get_id();
    int my_x = my_id % num_cols;
    int my_y = my_id / num_cols;

    int dest_id = route.dest_router;
    int dest_x = dest_id % num_cols;
    int dest_y = dest_id / num_cols;

    int x_hops = abs(dest_x - my_x);
    int y_hops = abs(dest_y - my_y);

    bool x_dirn = (dest_x >= my_x);
    bool y_dirn = (dest_y >= my_y);

    // already checked that in outportCompute() function
    assert(!(x_hops == 0 && y_hops == 0));

    if (x_hops > 0) {
        if (x_dirn) {
            assert(inport_dirn == "Local" || inport_dirn == "West");
            outport_dirn = "East";
        } else {
            assert(inport_dirn == "Local" || inport_dirn == "East");
            outport_dirn = "West";
        }
    } else if (y_hops > 0) {
        if (y_dirn) {
            // "Local" or "South" or "West" or "East"
            assert(inport_dirn != "North");
            outport_dirn = "North";
        } else {
            // "Local" or "North" or "West" or "East"
            assert(inport_dirn != "South");
            outport_dirn = "South";
        }
    } else {
        // x_hops == 0 and y_hops == 0
        // this is not possible
        // already checked that in outportCompute() function
        panic("x_hops == y_hops == 0");
    }

    return m_outports_dirn2idx[outport_dirn];
}

// Routing for 3D Cube
int
RoutingUnit::outportComputeZXY(RouteInfo route,
                               int inport,
                               PortDirection inport_dirn)
{
    PortDirection outport_dirn = "Unknown";

    [[maybe_unused]] int num_rows = m_router->get_net_ptr()->getNumRows();
    int num_cols = m_router->get_net_ptr()->getNumCols();
    int num_layers = m_router->get_net_ptr()->getNumLayers();
    assert(num_rows > 0 && num_cols > 0 && num_layers > 0);

    int my_id = m_router->get_id();
    int my_x = my_id % num_cols;
    int my_y = (my_id / num_cols) % num_rows;
    int my_z = my_id / (num_cols * num_rows);

    int dest_id = route.dest_router;
    int dest_x = dest_id % num_cols;
    int dest_y = (dest_id / num_cols) % num_rows;
    int dest_z = dest_id / (num_cols * num_rows);

    int x_hops = abs(dest_x - my_x);
    int y_hops = abs(dest_y - my_y);
    int z_hops = abs(dest_z - my_z);

    bool x_dirn = (dest_x >= my_x);
    bool y_dirn = (dest_y >= my_y);
    bool z_dirn = (dest_z >= my_z);

    // already checked that in outportCompute() function
    assert(!(x_hops == 0 && y_hops == 0 && z_hops == 0));

    if (z_hops > 0) {
        if (z_dirn) {
            assert(inport_dirn == "Local" || inport_dirn == "00-");
            outport_dirn = "00+";
        } else {
            assert(inport_dirn == "Local" || inport_dirn == "00+");
            outport_dirn = "00-";
        }
    } else if (x_hops > 0) {
        if (x_dirn) {
            assert(inport_dirn == "Local" || inport_dirn == "-00" || inport_dirn == "00-" || inport_dirn == "00+");
            outport_dirn = "+00";
        } else {
            assert(inport_dirn == "Local" || inport_dirn == "+00" || inport_dirn == "00-" || inport_dirn == "00+");
            outport_dirn = "-00";
        }
    } else if (y_hops > 0) {
        if (y_dirn) {
            assert(inport_dirn != "0+0");
            outport_dirn = "0+0";
        } else {
            assert(inport_dirn != "0-0");
            outport_dirn = "0-0";
        }
    } else {
        // x_hops == 0 and y_hops == 0 and z_hops == 0
        // this is not possible
        // already checked that in outportCompute() function
        panic("x_hops == y_hops == z_hops == 0");
    }

    return m_outports_dirn2idx[outport_dirn];
}

char FCCXYDirn(int my_x, int dest_x, int z_hops, int max_x, int max_z, bool adaptive) {
    assert (abs(dest_x - my_x) % 2 == z_hops % 2);

    if (my_x == 0) return '+';
    if (my_x == max_x - 1) return '-';

    if (abs(my_x - dest_x) >= z_hops) {
        return my_x < dest_x ? '+' : '-';
    }

    if (!adaptive) return '-';

    static const auto route_count = [&]() -> std::vector<std::vector<std::vector<int>>> {
        std::vector<std::vector<std::vector<int>>> rc(max_x, std::vector<std::vector<int>>(max_x, std::vector<int>(max_z, 0)));
        for (int x0 = 0; x0 < max_x; x0++) {
            rc[x0][x0][0] = 1;
            for (int z = 1; z < max_z; z++) {
                for (int x = 0; x < max_x; x++) {
                    if (x > 0) rc[x0][x][z] += rc[x0][x - 1][z - 1];
                    if (x < max_x - 1) rc[x0][x][z] += rc[x0][x + 1][z - 1];
                }
            }
        }
        return rc;
    }();

    int left_routes = route_count[dest_x][my_x - 1][z_hops - 1];
    int right_routes = route_count[dest_x][my_x + 1][z_hops - 1];

    assert (left_routes > 0 && right_routes > 0);

    static std::mt19937 gen(12345);
    std::uniform_int_distribution<int> dis(0, left_routes + right_routes - 1);
    int choice = dis(gen);

    if (choice < left_routes) return '-';
    else return '+';
}

// Custom Routing for FaceCenteredPacking
int
RoutingUnit::outportComputeFCC(RouteInfo route,
                                  int inport,
                                  PortDirection inport_dirn,
                                  bool adaptive)
{
    PortDirection outport_dirn = "Unknown";

    [[maybe_unused]] int num_rows = m_router->get_net_ptr()->getNumRows();
    int num_cols = m_router->get_net_ptr()->getNumCols();
    int num_layers = m_router->get_net_ptr()->getNumLayers();
    assert(num_rows > 0 && num_cols > 0 && num_layers > 0);

    int my_id = m_router->get_id();
    int my_x = my_id % num_cols;
    int my_y = (my_id / num_cols) % num_rows;
    int my_z = my_id / (num_cols * num_rows);

    int dest_id = route.dest_router;
    int dest_x = dest_id % num_cols;
    int dest_y = (dest_id / num_cols) % num_rows;
    int dest_z = dest_id / (num_cols * num_rows);

    int x_hops = abs(dest_x - my_x);
    int y_hops = abs(dest_y - my_y);
    int z_hops = abs(dest_z - my_z);

    bool x_dirn = (dest_x >= my_x);
    bool y_dirn = (dest_y >= my_y);
    bool z_dirn = (dest_z >= my_z);

    // already checked that in outportCompute() function
    assert(!(x_hops == 0 && y_hops == 0 && z_hops == 0));

    if (z_hops > 0) {
        // Actual XY coordinates
        my_x = my_x * 2 + my_z % 2;
        my_y = my_y * 2 + my_z % 2;
        dest_x = dest_x * 2 + dest_z % 2;
        dest_y = dest_y * 2 + dest_z % 2;

        outport_dirn = "000";

        outport_dirn[0] = FCCXYDirn(my_x, dest_x, z_hops, num_cols * 2, num_layers, adaptive);
        outport_dirn[1] = FCCXYDirn(my_y, dest_y, z_hops, num_rows * 2, num_layers, adaptive);

        if (z_dirn) {
            assert(inport_dirn == "Local" || inport_dirn[2] == '-');
            outport_dirn[2] = '+';
        } else {
            assert(inport_dirn == "Local" || inport_dirn[2] == '+');
            outport_dirn[2] = '-';
        }

        // printf("(%d, %d, %d) -> (%d, %d, %d) -> %s\n", my_x, my_y, my_z, dest_x, dest_y, dest_z, outport_dirn.c_str());
    } else if (x_hops > 0) {
        if (x_dirn) {
            assert(inport_dirn == "Local" || inport_dirn == "-00" || inport_dirn[2] != '0');
            outport_dirn = "+00";
        } else {
            assert(inport_dirn == "Local" || inport_dirn == "+00" || inport_dirn[2] != '0');
            outport_dirn = "-00";
        }
    } else if (y_hops > 0) {
        if (y_dirn) {
            assert(inport_dirn != "0+0");
            outport_dirn = "0+0";
        } else {
            assert(inport_dirn != "0-0");
            outport_dirn = "0-0";
        }
    } else {
        // x_hops == 0 and y_hops == 0 and z_hops == 0
        // this is not possible
        // already checked that in outportCompute() function
        panic("x_hops == y_hops == z_hops == 0");
    }

    return m_outports_dirn2idx[outport_dirn];
}

char BCCXYDirn(int my_x, int dest_x, int z_hops, int max_x, int max_z, bool adaptive){
    assert (abs(my_x - dest_x) % 2 == z_hops % 2);

    if (my_x == 0) return '+';
    if (my_x == max_x - 1) return '-';

    if (abs(my_x - dest_x) >= z_hops) {
        return my_x < dest_x ? '+' : '-';
    }

    if (!adaptive) return '-';

    static std::mt19937 gen(12345);
    std::uniform_int_distribution<int> dis(0, 1);
    int choice = dis(gen);

    if (choice == 0) return '-';
    else return '+';
}

char BCCYDirn(int my_y, int dest_y, int x_hops, int max_y, int max_x, bool adaptive){
    assert (abs(my_y - dest_y) % 2 == x_hops % 2);

    if (my_y == 0) return '+';
    if (my_y == max_y - 1) return '-';

    if (abs(my_y - dest_y) >= x_hops) {
        return my_y < dest_y ? '+' : '-';
    }

    if (!adaptive) return '-';

    static std::mt19937 gen(12345);
    std::uniform_int_distribution<int> dis(0, 1);
    int choice = dis(gen);

    if (choice == 0) return '-';
    else return '+';
}

int
RoutingUnit::outportComputeBCC(RouteInfo route,
                                  int inport,
                                  PortDirection inport_dirn,
                                  bool adaptive)
{
    [[maybe_unused]] int num_rows = m_router->get_net_ptr()->getNumRows();
    int num_cols = m_router->get_net_ptr()->getNumCols();
    int num_layers = m_router->get_net_ptr()->getNumLayers();
    assert(num_rows > 0 && num_cols > 0 && num_layers > 0);

    int my_id = m_router->get_id();
    int my_x = my_id % num_cols;
    int my_y = (my_id / num_cols) % num_rows;
    int my_z = my_id / (num_cols * num_rows);

    my_x = my_x * 2 + my_z % 2;
    my_y = my_y * 2 + my_z % 2;

    int dest_id = route.dest_router;
    int dest_x = dest_id % num_cols;
    int dest_y = (dest_id / num_cols) % num_rows;
    int dest_z = dest_id / (num_cols * num_rows);

    dest_x = dest_x * 2 + dest_z % 2;
    dest_y = dest_y * 2 + dest_z % 2;

    int x_hops = abs(dest_x - my_x);
    int y_hops = abs(dest_y - my_y);
    int z_hops = abs(dest_z - my_z);

    bool x_dirn = (dest_x >= my_x);
    bool y_dirn = (dest_y >= my_y);
    bool z_dirn = (dest_z >= my_z);

    bool x_maj = x_hops >= y_hops && x_hops >= z_hops;
    [[maybe_unused]] bool y_maj = y_hops >= x_hops && y_hops >= z_hops;
    bool z_maj = z_hops >= x_hops && z_hops >= y_hops;

    // already checked that in outportCompute() function
    assert(!(x_hops == 0 && y_hops == 0 && z_hops == 0));

    std::string outport_dirn = "000";

    if (z_maj) {
        
        outport_dirn[2] = z_dirn ? '+' : '-';

        outport_dirn[0] = BCCXYDirn(my_x, dest_x, z_hops, num_cols * 2, num_layers, adaptive);
        outport_dirn[1] = BCCXYDirn(my_y, dest_y, z_hops, num_rows * 2, num_layers, adaptive);

    } else if (my_z == num_layers - 1) {
        // Top layer special alg for z
        outport_dirn[2] = '-';
        if (x_maj) {
            outport_dirn[0] = x_dirn ? '+' : '-';
            outport_dirn[1] = BCCYDirn(my_y, dest_y, x_hops, num_rows * 2, num_layers, adaptive);
        } else if (my_x == 2 * num_cols - 1) {
            outport_dirn[0] = '-';
            outport_dirn[1] = y_dirn ? '+' : '-';
        } else { // y_maj
            outport_dirn[0] = '+'; // Increase x first
            outport_dirn[1] = BCCYDirn(my_y, dest_y, x_hops, num_rows * 2, num_layers, adaptive);
        }

    } else { // Increase z first
        outport_dirn[2] = '+';
        outport_dirn[0] = BCCXYDirn(my_x, dest_x, z_hops, num_cols * 2, num_layers, adaptive);
        outport_dirn[1] = BCCXYDirn(my_y, dest_y, z_hops, num_rows * 2, num_layers, adaptive);
    }

    // printf("%s\n", outport_dirn.c_str());

    return m_outports_dirn2idx[outport_dirn];
}

} // namespace garnet
} // namespace ruby
} // namespace gem5
