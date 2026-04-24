import os
import pandas as pd
import matplotlib.pyplot as plt
from matplotlib.patches import Patch

CASES = ["Case0", "Case1", "Case2", "Case3"]


def safe_read(path, columns):
    try:
        df = pd.read_csv(path, header=None, skipinitialspace=True)
        df.columns = columns
        return df
    except pd.errors.EmptyDataError:
        return pd.DataFrame(columns=columns)


def read_case(case):
    wh = safe_read(f"{case}/warehouse.csv", ["x", "y"])
    obs = safe_read(f"{case}/obstacles.csv", ["x", "y", "w", "d"])
    bays = safe_read(f"{case}/types_of_bays.csv", ["id", "w", "d", "h", "gap", "loads", "price"])
    sol = pd.read_csv(f"{case}/solution.csv")

    return wh.astype(int), obs.astype(int), bays.astype(int), sol.astype(int)


def draw_case(case, ax):
    wh, obs, bays, sol = read_case(case)

    px = list(wh["x"]) + [wh["x"].iloc[0]]
    py = list(wh["y"]) + [wh["y"].iloc[0]]
    ax.plot(px, py, linewidth=2.5, color="black", zorder=5)

    for _, o in obs.iterrows():
        ax.add_patch(
            plt.Rectangle(
                (o["x"], o["y"]), o["w"], o["d"],
                facecolor="red", edgecolor="darkred",
                alpha=0.45, linewidth=1.5, zorder=4
            )
        )
        ax.text(
            o["x"] + o["w"] / 2, o["y"] + o["d"] / 2,
            "OBS", ha="center", va="center",
            fontsize=7, fontweight="bold", color="darkred", zorder=6
        )

    cmap = plt.get_cmap("tab20")
    unique_ids = sorted(sol["Id"].unique())
    color_by_id = {bid: cmap(i % 20) for i, bid in enumerate(unique_ids)}

    for idx, s in sol.iterrows():
        bay = bays[bays["id"] == s["Id"]].iloc[0]

        w = int(bay["w"])
        d = int(bay["d"])
        gap = int(bay["gap"])

        if int(s["Rotation"]) == 1:
            w, d = d, w

        x = int(s["X"])
        y = int(s["Y"])
        bid = int(s["Id"])
        color = color_by_id[bid]

        ax.add_patch(
            plt.Rectangle(
                (x, y), w, d,
                facecolor=color, edgecolor="black",
                alpha=0.55, linewidth=1.0, zorder=2
            )
        )

        ax.text(
            x + w / 2, y + d / 2,
            str(bid),
            ha="center", va="center",
            fontsize=6, color="black",
            bbox=dict(facecolor="white", alpha=0.65, edgecolor="none", pad=0.5),
            zorder=7
        )

        if gap > 0:
            if int(s["Rotation"]) == 0:
                gx, gy, gw, gd = x, y + d, w, gap
                start = (gx + gw / 2, gy + gd / 2)
                end = (x + w / 2, y + d)
            else:
                gx, gy, gw, gd = x + w, y, gap, d
                start = (gx + gw / 2, gy + gd / 2)
                end = (x + w, y + d / 2)

            ax.add_patch(
                plt.Rectangle(
                    (gx, gy), gw, gd,
                    facecolor="none", edgecolor="blue",
                    linestyle="--", linewidth=0.9, zorder=3
                )
            )

            ax.annotate(
                "", xy=end, xytext=start,
                arrowprops=dict(arrowstyle="->", color="blue", linewidth=0.8),
                zorder=8
            )

    ax.set_title(case, fontsize=13, fontweight="bold")
    ax.set_aspect("equal", adjustable="box")
    ax.grid(True, linewidth=0.3, alpha=0.35)
    ax.set_xlabel("X")
    ax.set_ylabel("Y")


def main():
    existing = [c for c in CASES if os.path.exists(f"{c}/solution.csv")]

    fig, axes = plt.subplots(2, 2, figsize=(16, 16))
    axes = axes.flatten()

    for ax in axes:
        ax.axis("off")

    for ax, case in zip(axes, existing):
        ax.axis("on")
        draw_case(case, ax)

    legend_items = [
        Patch(facecolor="red", edgecolor="darkred", alpha=0.45, label="Obstacle"),
        Patch(facecolor="gray", edgecolor="black", alpha=0.55, label="Bay"),
        Patch(facecolor="none", edgecolor="blue", linestyle="--", label="Gap / access"),
        Patch(facecolor="none", edgecolor="black", label="Warehouse boundary"),
    ]

    fig.legend(handles=legend_items, loc="upper center", ncol=4, fontsize=11)
    fig.suptitle("Warehouse solutions", fontsize=18, fontweight="bold")
    plt.tight_layout(rect=[0, 0, 1, 0.96])
    plt.savefig("all_solutions.png", dpi=200)
    plt.show()

    print("Guardado: all_solutions.png")


if __name__ == "__main__":
    main()