import subprocess
import re
import os
import sys
import argparse
import concurrent.futures
import shutil
import json
import pandas as pd
import matplotlib.pyplot as plt
from matplotlib.ticker import ScalarFormatter


class SimulationConfig:
    """
    Configuration class for gem5 network simulations.
    """
    def __init__(self, synthetic="uniform_random", link_latency=1, router_latency=1, link_width_bits=128, 
                 vcs_per_vnet=4, topology="Mesh_XY", wormhole=False):
        self.synthetic = synthetic
        self.link_latency = link_latency
        self.router_latency = router_latency
        self.link_width_bits = link_width_bits
        self.vcs_per_vnet = vcs_per_vnet
        self.topology = topology
        self.wormhole = wormhole


    def to_tuple(self):
        """
        Convert the configuration to a tuple for use as a key in data storage.
        """
        return (self.synthetic, self.link_latency, self.router_latency, self.link_width_bits, 
                self.vcs_per_vnet, self.topology, self.wormhole)

    def __str__(self):
        """
        String representation of the configuration.
        """
        return (f"Synthetic traffic: {self.synthetic}, "
                f"Link latency: {self.link_latency}, "
                f"Router latency: {self.router_latency}, "
                f"Link width: {self.link_width_bits} bits, "
                f"VCs per vnet: {self.vcs_per_vnet}, "
                f"Topology: {self.topology}, "
                f"Routing: {'wormhole' if self.wormhole else 'normal'}, "
)


def run(injection_rate, config=None, output_dir="m5out"):
    """
    Runs a gem5 simulation with the given synthetic traffic type and injection rate.

    Args:
        injection_rate (float): The packet injection rate.
        config (SimulationConfig): The simulation configuration.
        output_dir (str): Directory where simulation output will be stored.

    Returns:
        tuple: A tuple containing the simulation results:
               (packets_received, avg_pkt_lat)
               Returns None if the simulation fails or the stats file is not found.
    """
    # Use default config if none provided
    if config is None:
        config = SimulationConfig()
    
    gem5_executable = "./build/NULL/gem5.opt"
    config_script = "configs/example/garnet_synth_traffic.py"

    # This script should be run from the root of the gem5 directory.
    if not os.path.exists(gem5_executable):
        print(
            f"Error: gem5 executable not found at {os.path.abspath(gem5_executable)}"
        )
        print("Please run this script from the root of the gem5 directory.")
        sys.exit(1)

    # Create output directory if it doesn't exist
    os.makedirs(output_dir, exist_ok=True)
    
    command = [
        gem5_executable,
        f"--outdir={output_dir}",
        config_script,
        "--network=garnet",
        "--inj-vnet=0",
        "--sim-cycles=10000",
        f"--synthetic={config.synthetic}",
        f"--injectionrate={injection_rate}",
        f"--link-latency={config.link_latency}",
        f"--router-latency={config.router_latency}",
        f"--link-width-bits={config.link_width_bits}",
        f"--vcs-per-vnet={config.vcs_per_vnet}",

    ]
    
    # Configure topology-specific parameters
    if config.topology == "Mesh_XY":
        command.extend([
            "--num-cpus=64",
            "--num-dirs=64",
            "--topology=Mesh_XY",
            "--mesh-rows=8"
        ])
    elif config.topology == "Ring":
        command.extend([
            "--num-cpus=16",
            "--num-dirs=16",
            "--topology=Ring",
            "--bubble",
            "--routing-algorithm=2"
        ])
    elif config.topology == "SimpleCubic":
        command.extend([
            "--num-cpus=128",
            "--num-dirs=128",
            "--topology=SimpleCubic",
            "--num-rows=4",
            "--num-cols=4",
            "--routing-algorithm=2"
        ])
    elif config.topology == "FaceCenteredCubicDeterministic":
        command.extend([
            "--num-cpus=128",
            "--num-dirs=128",
            "--topology=FaceCenteredCubic",
            "--num-rows=4",
            "--num-cols=4",
            "--routing-algorithm=3"
        ])
    elif config.topology == "FaceCenteredCubicAdaptive":
        command.extend([
            "--num-cpus=128",
            "--num-dirs=128",
            "--topology=FaceCenteredCubic",
            "--num-rows=4",
            "--num-cols=4",
            "--routing-algorithm=4"
        ])
    elif config.topology == "BodyCenteredCubicDeterministic":
        command.extend([
            "--num-cpus=128",
            "--num-dirs=128",
            "--topology=BodyCenteredCubic",
            "--num-rows=4",
            "--num-cols=4",
            "--routing-algorithm=5"
        ])
    elif config.topology == "BodyCenteredCubicAdaptive":
        command.extend([
            "--num-cpus=128",
            "--num-dirs=128",
            "--topology=BodyCenteredCubic",
            "--num-rows=4",
            "--num-cols=4",
            "--routing-algorithm=6"
        ])
    
    
    # Add wormhole option if enabled
    if config.wormhole:
        command.append("--wormhole")

    print(
        f"Running gem5 with synthetic={config.synthetic} and injection_rate={injection_rate}..."
    )

    process = subprocess.run(command, capture_output=True, text=True)

    if process.returncode != 0:
        print("Error running gem5 simulation.")
        print("Stdout:", process.stdout)
        print("Stderr:", process.stderr)
        return None

    print("gem5 simulation finished successfully.")

    stats_file = f"{output_dir}/stats.txt"
    if not os.path.exists(stats_file):
        print(f"Error: stats file not found at {stats_file}")
        return None

    stat_patterns = {
        "packets_received": r"system\.ruby\.network\.packets_received::total\s+(\d+)",
        "avg_pkt_lat": r"system\.ruby\.network\.average_packet_latency\s+([0-9.]+)"
    }

    extracted_values = {}
    with open(stats_file, "r") as f:
        stats_content = f.read()
        for stat_name, pattern in stat_patterns.items():
            match = re.search(pattern, stats_content)
            if match:
                value_str = match.group(1)
                try:
                    extracted_values[stat_name] = float(value_str)
                except ValueError:
                    extracted_values[stat_name] = int(value_str)
            else:
                print(
                    f"Warning: Could not find stat '{stat_name}' in {stats_file}"
                )
                extracted_values[stat_name] = None

    result_tuple = (
        extracted_values.get("packets_received"),
        extracted_values.get("avg_pkt_lat")
    )

    return result_tuple


def run_tests(config=None, overwrite=False):
    """
    Runs multiple gem5 simulations with varying injection rates using multithreading.
    Results are stored in a single JSON file with key-value pairs.
    
    Args:
        config (SimulationConfig): The simulation configuration.
        overwrite (bool): If True, always overwrite existing data. 
                         If False, check if existing data is complete (100 points).
                         If complete, skip. If not complete, rerun all 100 instead of only missing.
    """
    # Use default config if none provided
    if config is None:
        config = SimulationConfig()
    
    # Define the configuration tuple as the key
    config_key = config.to_tuple()
    
    # Use a single JSON file for all data storage
    data_file = "simulation_data.json"
    
    # Load existing data or create new structure
    if os.path.exists(data_file):
        with open(data_file, "r") as f:
            all_data = json.load(f)
    else:
        all_data = {}
    
    # Convert tuple keys in JSON back to tuples (JSON stores them as strings)
    converted_data = {}
    for key_str, value in all_data.items():
        # Convert string representation back to tuple
        key_tuple = tuple(eval(key_str))
        converted_data[key_tuple] = value
    all_data = converted_data
    
    # Initialize array for this configuration if it doesn't exist
    if config_key not in all_data:
        all_data[config_key] = []
    
    print(f"Starting tests with parameters:\\n"
          f"  {config}")
    
    # Define the range of injection rates to test
    injection_rates = [round(0.005 * i, 3) for i in range(1, 101)]
    
    # Check if we should overwrite or handle existing data conditionally
    if not overwrite and len(all_data[config_key]) == 100:
        print("All injection rates already completed for this configuration.")
        return
    
    # Store new results
    results = []
    
    # Define a worker function to run a single simulation
    def run_simulation(injection_rate):
        # Use temporary directory for simulation output
        temp_dir = f"temp_run_{config.synthetic}_{injection_rate}"
        results = run(
            injection_rate=injection_rate,
            config=config,
            output_dir=temp_dir
        )

        # Clean up temporary directory
        if os.path.exists(temp_dir):
            shutil.rmtree(temp_dir)
        
        if results:
            return (injection_rate, results[0], results[1])
        else:
            print(f"  Failed for injection_rate: {injection_rate}")
            return None
    
    # Use ThreadPoolExecutor to run simulations in parallel
    with concurrent.futures.ThreadPoolExecutor() as executor:
        # Submit all tasks and store the futures
        future_to_injection_rate = {
            executor.submit(run_simulation, injection_rate): injection_rate 
            for injection_rate in injection_rates
        }
        
        # Collect results as they complete
        for future in concurrent.futures.as_completed(future_to_injection_rate):
            result = future.result()
            if result:
                results.append(result)
    
    # Append new results to existing data
    all_data[config_key] = results
    
    # Sort the data for this configuration by injection rate
    all_data[config_key].sort(key=lambda x: x[0])
    
    # Save the updated data back to JSON file
    # Convert tuple keys to strings for JSON serialization
    json_compatible_data = {str(k): v for k, v in all_data.items()}
    
    with open(data_file, "w") as f:
        json.dump(json_compatible_data, f, indent=2)
    
    print(f"All data saved to {data_file}")


def plot(configs, data_file="simulation_data.json", filename="plot.png", title="Throughput vs Latency", label_type="synthetic", max_rate=0.5, ymin=None, ymax=None):
    """
    Plot throughput vs latency for a list of configurations on the same graph.
    
    Args:
        configs (list): List of SimulationConfig objects.
        max_rate (float): Maximum injection rate to include in the plot (default: 0.5)
        filename (str): Output filename for the plot (default: "plot.png")
        title (str): Title for the plot (default: "Throughput vs Latency")
    
    Returns:
        matplotlib.figure.Figure: The plot figure object
    """
    if not os.path.exists(data_file):
        print(f"Data file {data_file} not found. Please run simulations first.")
        return None
    
    # Load the data
    with open(data_file, "r") as f:
        all_data = json.load(f)
    
    # Convert string keys back to tuples
    converted_data = {}
    for key_str, value in all_data.items():
        key_tuple = tuple(eval(key_str))
        converted_data[key_tuple] = value
    all_data = converted_data
    
    # Create the plot
    plt.figure(figsize=(12, 8))
    
    # Plot data for each configuration
    for i, config in enumerate(configs):
        # Get config tuple
        config_tuple = config.to_tuple()
        
        # Check if the configuration exists
        if config_tuple not in all_data:
            print(f"Configuration {config_tuple} not found in data.")
            print("Available configurations:")
            for config_key in all_data.keys():
                print(f"  {config_key}")
            continue
        
        # Get the data for this configuration
        config_data = all_data[config_tuple]
        
        if not config_data:
            print(f"No data available for configuration {config_tuple}")
            continue
        
        # Create DataFrame from the data
        df = pd.DataFrame(config_data, columns=['injection_rate', 'packets_received', 'avg_pkt_lat'])
        
        # Filter data based on max_rate
        df = df[df['injection_rate'] <= max_rate]
        
        if df.empty:
            print(f"No data points for configuration with injection_rate <= {max_rate}")
            continue
        
        # Calculate throughput and latency
        df['throughput'] = df['packets_received'] / 10000
        df['latency'] = df['avg_pkt_lat']
        
        # Create descriptive label from configuration
        if label_type == "synthetic":
            label = f"{config.synthetic}"
        elif label_type == "latency":
            label = f"{config.router_latency} cycles"
        elif label_type == "width":
            label = f"{config.link_width_bits} bits"
        elif label_type == "vc":
            label = f"{config.vcs_per_vnet} VCs"
        elif label_type == "wormhole":
            if config.wormhole and config.vcs_per_vnet == 1 and config.buffers_per_ctrl_vc == 16:
                label = "1 VCs, 16 Depth (Wormhole)"
            elif not config.wormhole and config.vcs_per_vnet == 16 and config.buffers_per_ctrl_vc == 1:
                label = "16 VCs, 1 Depth"
            elif not config.wormhole and config.vcs_per_vnet == 1 and config.buffers_per_ctrl_vc == 1:
                label = "1 VC, 1 Depth"
            else:
                raise ValueError(f"Unknown configuration: {config}")
        elif label_type == "topology":
            label = f"{config.topology}"
        
        # Plot the data
        plt.plot(df['throughput'], df['latency'], marker='o', linestyle='-', linewidth=2, markersize=6, label=label)
    
    # Add labels and title
    plt.xlabel('Throughput (packets/cycle)')
    plt.ylabel('Latency (cycles)')
    
    # Set x and y axis limits
    plt.xlim(left=0)
    if ymin is not None:
        plt.ylim(bottom=ymin)
    if ymax is not None:
        plt.ylim(top=ymax)
    
    plt.title(title)
    
    # Add legend, grid and layout improvements
    plt.legend()
    plt.grid(True, alpha=0.3)
    plt.tight_layout()
    
    # Save the plot
    plt.savefig(filename)


if __name__ == "__main__":
    configs = [
        SimulationConfig(synthetic='uniform_random'),
        SimulationConfig(synthetic='shuffle'),
        SimulationConfig(synthetic='transpose'),
        SimulationConfig(synthetic='tornado'),
        SimulationConfig(synthetic='neighbor')
    ]
    for config in configs:
        run_tests(config=config)
    plot(configs)
