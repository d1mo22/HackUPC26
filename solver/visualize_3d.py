import os
import math
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


def rotate_point(px, py, angle_deg):
    a = math.radians(angle_deg)
    ca = math.cos(a)
    sa = math.sin(a)

    return px * ca - py * sa, px * sa + py * ca


def rect_points_2d(x, y, w, d, angle_deg):
    local = [
        (0, 0),
        (w, 0),
        (w, d),
        (0, d),
    ]

    points = []

    for px, py in local:
        rx, ry = rotate_point(px, py, angle_deg)
        points.append((x + rx, y + ry))

    return points


def prism_faces(points2d, z, h):
    bottom = [(x, y, z) for x, y in points2d]
    top = [(x, y, z + h) for x, y in points2d]

    faces = []

    faces.append(bottom)
    faces.append(top)

    n = len(points2d)

    for i in range(n):
        faces.append([
            bottom[i],
            bottom[(i + 1) % n],
            top[(i + 1) % n],
            top[i],
        ])

    return faces


def add_prism(ax, points2d, z, h, color, alpha=0.45, edgecolor="black", linewidth=0.4):
    faces = prism_faces(points2d, z, h)

    poly = Poly3DCollection(
        faces,
        facecolors=color,
        edgecolors=edgecolor,
        linewidths=linewidth,
        alpha=alpha
    )

    ax.add_collection3d(poly)


def gap_points_2d(x, y, w, d, gap, angle_deg):
    if gap <= 0:
        return None

    local = [
        (0, d),
        (w, d),
        (w, d + gap),
        (0, d + gap),
    ]

    points = []

    for px, py in local:
        rx, ry = rotate_point(px, py, angle_deg)
        points.append((x + rx, y + ry))

    return points


def centroid(points):
    sx = sum(p[0] for p in points)
    sy = sum(p[1] for p in points)
    return sx / len(points), sy / len(points)


def draw_floor_polygon(ax, wh):
    xs = list(wh["x"]) + [wh["x"].iloc[0]]
    ys = list(wh["y"]) + [wh["y"].iloc[0]]
    zs = [0] * len(xs)

    ax.plot(xs, ys, zs, color="black", linewidth=2.2)


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
            alpha=0.10,
            linewidths=0.5
        )

        ax.add_collection3d(surf)

        ax.plot([x1, x2], [min_y, min_y], [h, h], color="cyan", linewidth=1)
        ax.plot([x1, x2], [max_y, max_y], [h, h], color="cyan", linewidth=1)


def draw_case_3d(case):
    wh, obs, ceiling, bays, sol = read_case(case)

    fig = plt.figure(figsize=(13, 10))
    ax = fig.add_subplot(111, projection="3d")

    draw_floor_polygon(ax, wh)
    draw_ceiling(ax, wh, ceiling)

    for _, o in obs.iterrows():
        x = int(o["x"])
        y = int(o["y"])
        w = int(o["w"])
        d = int(o["d"])

        pts = rect_points_2d(x, y, w, d, 0)
        add_prism(
            ax,
            pts,
            0,
            500,
            color="red",
            alpha=0.35,
            edgecolor="darkred",
            linewidth=0.6
        )

    cmap = plt.get_cmap("tab20")
    unique_ids = sorted(sol["Id"].unique())
    color_by_id = {bid: cmap(i % 20) for i, bid in enumerate(unique_ids)}

    all_x = list(wh["x"])
    all_y = list(wh["y"])

    for _, s in sol.iterrows():
        bid = int(s["Id"])
        bay = bays[bays["id"] == bid].iloc[0]

        w = int(bay["w"])
        d = int(bay["d"])
        h = int(bay["h"])
        gap = int(bay["gap"])

        x = int(s["X"])
        y = int(s["Y"])
        angle = int(s["Rotation"])

        bay_pts = rect_points_2d(x, y, w, d, angle)

        add_prism(
            ax,
            bay_pts,
            0,
            h,
            color=color_by_id[bid],
            alpha=0.65,
            edgecolor="black",
            linewidth=0.35
        )

        cx, cy = centroid(bay_pts)

        ax.text(
            cx,
            cy,
            h + 100,
            str(bid),
            ha="center",
            va="center",
            fontsize=7
        )

        all_x.extend([p[0] for p in bay_pts])
        all_y.extend([p[1] for p in bay_pts])

        gap_pts = gap_points_2d(x, y, w, d, gap, angle)

        if gap_pts is not None:
            add_prism(
                ax,
                gap_pts,
                0,
                80,
                color="blue",
                alpha=0.18,
                edgecolor="blue",
                linewidth=0.35
            )

            all_x.extend([p[0] for p in gap_pts])
            all_y.extend([p[1] for p in gap_pts])

    ax.set_title(f"{case} - 3D warehouse solution with arbitrary angles")
    ax.set_xlabel("X")
    ax.set_ylabel("Y")
    ax.set_zlabel("Height")

    margin = 500

    ax.set_xlim(min(all_x) - margin, max(all_x) + margin)
    ax.set_ylim(min(all_y) - margin, max(all_y) + margin)

    max_z = max(
        int(ceiling["h"].max()) if len(ceiling) else 1000,
        int(bays["h"].max()) if len(bays) else 1000
    )

    ax.set_zlim(0, max_z * 1.15)

    ax.view_init(elev=28, azim=-55)

    plt.tight_layout()

    out = f"{case}_3d_angles.png"
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