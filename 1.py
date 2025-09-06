from test import run_tests, SimulationConfig, plot

def main():
    configs = [
        SimulationConfig(topology="Cubic"),
        SimulationConfig(topology="CubicClosePackingDeterministic"),
        SimulationConfig(topology="CubicClosePackingAdaptive")
    ]

    for config in configs:
        run_tests(config=config)

    plot(configs, filename="plot.png", title="Throughput vs Latency", label_type="topology", max_rate=1, ymin=0, ymax=100)

if __name__ == "__main__":
    main()
