import os
import pandas as pd
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d.art3d import Poly3DCollection

CASES = ["Case0", "Case1", "Case2", "Case3"]


def safe_read(path, columns):
    try:
        df = pd.read_csv(path, header=None, skipinitialspace=True)
        df.columns = columns
        return df.astype(int)
    except pd.errors.EmptyDataError:
        return pd.DataFrame(columns=columns).astype(int)


def read_case(case):
    wh = safe_read(f"{case}/warehouse.csv", ["x", "y"])
    obs = safe_read(f"{case}/obstacles.csv", ["x", "y", "w", "d"])
    ceiling = safe_read(f"{case}/ceiling.csv", ["x", "h"])
    bays = safe_read(f"{case}/types_of_bays.csv", ["id", "w", "d", "h", "gap", "loads", "price"])
    sol = pd.read_csv(f"{case}/solution.csv").astype(int)

    return wh, obs, ceiling, bays, sol


def cuboid_faces(x, y, z, w, d, h):
    p = [
        (x,     y,     z),
        (x+w,   y,     z),
        (x+w,   y+d,   z),
        (x,     y+d,   z),
        (x,     y,     z+h),
        (x+w,   y,     z+h),
        (x+w,   y+d,   z+h),
        (x,     y+d,   z+h),
    ]

    return [
        [p[0], p[1], p[2], p[3]],
        [p[4], p[5], p[6], p[7]],
        [p[0], p[1], p[5], p[4]],
        [p[1], p[2], p[6], p[5]],
        [p[2], p[3], p[7], p[6]],
        [p[3], p[0], p[4], p[7]],
    ]


def add_box(ax, x, y, z, w, d, h, color, alpha=0.45, edgecolor="black"):
    faces = cuboid_faces(x, y, z, w, d, h)
    box = Poly3DCollection(
        faces,
        facecolors=color,
        edgecolors=edgecolor,
        linewidths=0.4,
        alpha=alpha
    )
    ax.add_collection3d(box)


def draw_floor_polygon(ax, wh):
    xs = list(wh["x"]) + [wh["x"].iloc[0]]
    ys = list(wh["y"]) + [wh["y"].iloc[0]]
    zs = [0] * len(xs)

    ax.plot(xs, ys, zs, color="black", linewidth=2)


def draw_ceiling(ax, wh, ceiling):
    min_y = int(wh["y"].min())
    max_y = int(wh["y"].max())

    ceiling = ceiling.sort_values("x").reset_index(drop=True)

    for i in range(len(ceiling)):
        x1 = int(ceiling.loc[i, "x"])
        h = int(ceiling.loc[i, "h"])

        if i + 1 < len(ceiling):
            x2 = int(ceiling.loc[i + 1, "x"])
        else:
            x2 = int(wh["x"].max())

        if x2 <= x1:
            continue

        verts = [[
            (x1, min_y, h),
            (x2, min_y, h),
            (x2, max_y, h),
            (x1, max_y, h),
        ]]

        surf = Poly3DCollection(
            verts,
            facecolors="cyan",
            edgecolors="cyan",
            alpha=0.12,
            linewidths=0.5
        )
        ax.add_collection3d(surf)

        ax.plot([x1, x2], [min_y, min_y], [h, h], color="cyan", linewidth=1)
        ax.plot([x1, x2], [max_y, max_y], [h, h], color="cyan", linewidth=1)


def draw_case_3d(case):
    wh, obs, ceiling, bays, sol = read_case(case)

    fig = plt.figure(figsize=(12, 9))
    ax = fig.add_subplot(111, projection="3d")

    draw_floor_polygon(ax, wh)
    draw_ceiling(ax, wh, ceiling)

    # Obstacles
    for _, o in obs.iterrows():
        x = int(o["x"])
        y = int(o["y"])
        w = int(o["w"])
        d = int(o["d"])

        add_box(ax, x, y, 0, w, d, 500, color="red", alpha=0.35, edgecolor="darkred")

    cmap = plt.get_cmap("tab20")
    unique_ids = sorted(sol["Id"].unique())
    color_by_id = {bid: cmap(i % 20) for i, bid in enumerate(unique_ids)}

    # Bays + gaps
    for _, s in sol.iterrows():
        bay = bays[bays["id"] == int(s["Id"])].iloc[0]

        w = int(bay["w"])
        d = int(bay["d"])
        h = int(bay["h"])
        gap = int(bay["gap"])

        rot = int(s["Rotation"])

        if rot == 1:
            w, d = d, w

        x = int(s["X"])
        y = int(s["Y"])
        bid = int(s["Id"])

        add_box(
            ax, x, y, 0,
            w, d, h,
            color=color_by_id[bid],
            alpha=0.60,
            edgecolor="black"
        )

        ax.text(
            x + w / 2,
            y + d / 2,
            h + 100,
            str(bid),
            ha="center",
            va="center",
            fontsize=7
        )

        # gap as blue transparent low rectangle
        if gap > 0:
            if rot == 0:
                gx, gy, gw, gd = x, y + d, w, gap
            else:
                gx, gy, gw, gd = x + w, y, gap, d

            add_box(
                ax, gx, gy, 0,
                gw, gd, 80,
                color="blue",
                alpha=0.18,
                edgecolor="blue"
            )

    ax.set_title(f"{case} - 3D warehouse solution")
    ax.set_xlabel("X")
    ax.set_ylabel("Y")
    ax.set_zlabel("Height")

    ax.set_xlim(int(wh["x"].min()), int(wh["x"].max()))
    ax.set_ylim(int(wh["y"].min()), int(wh["y"].max()))

    max_z = max(
        int(ceiling["h"].max()) if len(ceiling) else 1000,
        int(bays["h"].max()) if len(bays) else 1000
    )
    ax.set_zlim(0, max_z * 1.15)

    ax.view_init(elev=28, azim=-55)

    plt.tight_layout()
    out = f"{case}_3d.png"
    plt.savefig(out, dpi=200)
    plt.show()

    print("Guardado:", out)


def main():
    for case in CASES:
        if os.path.exists(f"{case}/solution.csv"):
            draw_case_3d(case)
        else:
            print(f"{case}: no existe solution.csv")


if __name__ == "__main__":
    main()