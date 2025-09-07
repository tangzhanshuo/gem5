from test import run_tests, SimulationConfig, plot

def main():
    configs = [
        SimulationConfig(topology="SimpleCubic", vcs_per_vnet=1),
        SimulationConfig(topology="FaceCenteredCubicDeterministic", vcs_per_vnet=1),
        SimulationConfig(topology="FaceCenteredCubicAdaptive", vcs_per_vnet=1),
        SimulationConfig(topology="BodyCenteredCubicDeterministic", vcs_per_vnet=1),
        SimulationConfig(topology="BodyCenteredCubicAdaptive", vcs_per_vnet=1)
    ]

    for config in configs:
        run_tests(config=config)

    plot(configs, filename=f"plot.png", title="Throughput vs Latency", label_type="topology", max_rate=0.5, ymin=0, ymax=100)

if __name__ == "__main__":
    main()
