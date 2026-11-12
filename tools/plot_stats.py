"""Plot one or more statistics CSVs written by `wsim --csv` (population, births, traits, generation)."""

import argparse
import csv

import matplotlib.pyplot as plt


def load(path: str) -> dict[str, list[float]]:
    with open(path) as f:
        rows = list(csv.DictReader(f))
    return {k: [float(r[k]) for r in rows] for k in rows[0]}


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("csv", nargs="+", help="label=path.csv or path.csv")
    ap.add_argument("--out", default="docs/evolution.png")
    args = ap.parse_args()

    panels = [
        ("population", "population"),
        ("births_and_respawns", "births / respawns (cumulative)"),
        ("mean_generation", "mean generation"),
        ("mean_temperature", "mean body temperature"),
        ("mean_heat_production", "mean heat production"),
        ("mean_size", "mean size"),
    ]
    fig, axes = plt.subplots(2, 3, figsize=(14, 7))
    for spec in args.csv:
        label, path = spec.split("=", 1) if "=" in spec else (spec, spec)
        d = load(path)
        for ax, (key, title) in zip(axes.flat, panels, strict=True):
            if key == "births_and_respawns":
                (line,) = ax.plot(d["step"], d["births"], label=f"{label}: births")
                ax.plot(d["step"], d["respawns"], "--", color=line.get_color(), label=f"{label}: respawns")
            else:
                ax.plot(d["step"], d[key], label=label)
            ax.set_title(title)
            ax.set_xlabel("step")
            ax.grid(alpha=0.3)
            ax.ticklabel_format(axis="x", style="sci", scilimits=(0, 0))
    for ax in axes.flat:
        ax.legend(fontsize=7)
    fig.tight_layout()
    fig.savefig(args.out, dpi=110)
    print("wrote", args.out)


if __name__ == "__main__":
    main()
