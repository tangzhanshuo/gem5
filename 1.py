from test import run_tests, SimulationConfig, plot

def main():
    for synthetic in ['neighbor']:
        configs = [
            SimulationConfig(topology="SimpleCubic", synthetic=synthetic),
            SimulationConfig(topology="CubicClosePackingDeterministic", synthetic=synthetic),
            SimulationConfig(topology="CubicClosePackingAdaptive", synthetic=synthetic)
        ]

        for config in configs:
            run_tests(config=config)

        plot(configs, filename=f"plot_{synthetic}.png", title="Throughput vs Latency", label_type="topology", max_rate=1, ymin=0, ymax=100)

if __name__ == "__main__":
    main()
