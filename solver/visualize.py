import os
import math
import pandas as pd
import matplotlib.pyplot as plt
from matplotlib.patches import Polygon as MplPolygon, Patch

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


def rotate_point(px, py, angle_deg):
    a = math.radians(angle_deg)
    ca = math.cos(a)
    sa = math.sin(a)

    return px * ca - py * sa, px * sa + py * ca


def rotated_rect_points(x, y, w, d, angle_deg):
    local = [
        (0, 0),
        (w, 0),
        (w, d),
        (0, d),
    ]

    pts = []

    for px, py in local:
        rx, ry = rotate_point(px, py, angle_deg)
        pts.append((x + rx, y + ry))

    return pts


def rotated_gap_points(x, y, w, d, gap, angle_deg):
    if gap <= 0:
        return None

    local = [
        (0, d),
        (w, d),
        (w, d + gap),
        (0, d + gap),
    ]

    pts = []

    for px, py in local:
        rx, ry = rotate_point(px, py, angle_deg)
        pts.append((x + rx, y + ry))

    return pts


def centroid(points):
    return (
        sum(p[0] for p in points) / len(points),
        sum(p[1] for p in points) / len(points),
    )


def draw_case(case, ax):
    wh, obs, bays, sol = read_case(case)

    px = list(wh["x"]) + [wh["x"].iloc[0]]
    py = list(wh["y"]) + [wh["y"].iloc[0]]
    ax.plot(px, py, linewidth=2.5, color="black", zorder=10)

    all_x = list(wh["x"])
    all_y = list(wh["y"])

    for _, o in obs.iterrows():
        ox = int(o["x"])
        oy = int(o["y"])
        ow = int(o["w"])
        od = int(o["d"])

        obs_pts = rotated_rect_points(ox, oy, ow, od, 0)

        ax.add_patch(
            MplPolygon(
                obs_pts,
                closed=True,
                facecolor="red",
                edgecolor="darkred",
                alpha=0.45,
                linewidth=1.5,
                zorder=7
            )
        )

        ax.text(
            ox + ow / 2,
            oy + od / 2,
            "OBS",
            ha="center",
            va="center",
            fontsize=7,
            fontweight="bold",
            color="darkred",
            zorder=12
        )

        all_x.extend([p[0] for p in obs_pts])
        all_y.extend([p[1] for p in obs_pts])

    cmap = plt.get_cmap("tab20")
    unique_ids = sorted(sol["Id"].unique())
    color_by_id = {bid: cmap(i % 20) for i, bid in enumerate(unique_ids)}

    for _, s in sol.iterrows():
        bid = int(s["Id"])
        bay = bays[bays["id"] == bid].iloc[0]

        w = int(bay["w"])
        d = int(bay["d"])
        gap = int(bay["gap"])

        x = int(s["X"])
        y = int(s["Y"])
        angle = int(s["Rotation"])

        color = color_by_id[bid]

        bay_pts = rotated_rect_points(x, y, w, d, angle)

        ax.add_patch(
            MplPolygon(
                bay_pts,
                closed=True,
                facecolor=color,
                edgecolor="black",
                alpha=0.58,
                linewidth=1.0,
                zorder=4
            )
        )

        cx, cy = centroid(bay_pts)

        ax.text(
            cx,
            cy,
            f"{bid}\n{angle}°",
            ha="center",
            va="center",
            fontsize=6,
            color="black",
            bbox=dict(facecolor="white", alpha=0.70, edgecolor="none", pad=0.5),
            zorder=13
        )

        all_x.extend([p[0] for p in bay_pts])
        all_y.extend([p[1] for p in bay_pts])

        gap_pts = rotated_gap_points(x, y, w, d, gap, angle)

        if gap_pts is not None:
            ax.add_patch(
                MplPolygon(
                    gap_pts,
                    closed=True,
                    facecolor="none",
                    edgecolor="blue",
                    linestyle="--",
                    linewidth=1.0,
                    zorder=5
                )
            )

            gcx, gcy = centroid(gap_pts)

            ax.annotate(
                "",
                xy=(cx, cy),
                xytext=(gcx, gcy),
                arrowprops=dict(arrowstyle="->", color="blue", linewidth=0.8),
                zorder=14
            )

            all_x.extend([p[0] for p in gap_pts])
            all_y.extend([p[1] for p in gap_pts])

    ax.set_title(case, fontsize=13, fontweight="bold")
    ax.set_aspect("equal", adjustable="box")
    ax.grid(True, linewidth=0.3, alpha=0.35)
    ax.set_xlabel("X")
    ax.set_ylabel("Y")

    margin = 500
    ax.set_xlim(min(all_x) - margin, max(all_x) + margin)
    ax.set_ylim(min(all_y) - margin, max(all_y) + margin)


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
        Patch(facecolor="gray", edgecolor="black", alpha=0.58, label="Bay rotated footprint"),
        Patch(facecolor="none", edgecolor="blue", linestyle="--", label="Gap / access rotated footprint"),
        Patch(facecolor="none", edgecolor="black", label="Warehouse boundary"),
    ]

    fig.legend(handles=legend_items, loc="upper center", ncol=4, fontsize=11)
    fig.suptitle("Warehouse solutions - true rotated footprints", fontsize=18, fontweight="bold")

    plt.tight_layout(rect=[0, 0, 1, 0.96])
    plt.savefig("all_solutions_angles.png", dpi=220)
    plt.show()

    print("Guardado: all_solutions_angles.png")


if __name__ == "__main__":
    main()