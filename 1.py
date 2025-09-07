from test import run_tests, SimulationConfig, plot

def main():
    traffic_types = ["uniform_random", "shuffle", "transpose", "tornado", "neighbor"]
    for synthetic in traffic_types:
        configs = [
            SimulationConfig(synthetic=synthetic, topology="SimpleCubic", vcs_per_vnet=1),
            SimulationConfig(synthetic=synthetic, topology="FaceCenteredCubicDeterministic", vcs_per_vnet=1),
            SimulationConfig(synthetic=synthetic, topology="FaceCenteredCubicRandom", vcs_per_vnet=1),
            SimulationConfig(synthetic=synthetic, topology="BodyCenteredCubic", vcs_per_vnet=1),
        ]

        for config in configs:
            run_tests(config=config)

        plot(configs, filename=f"plot_{synthetic}.png", title=f"Throughput vs Latency ({synthetic} Traffic)", label_type="topology", max_rate=0.5, ymin=0, ymax=100)

if __name__ == "__main__":
    main()
