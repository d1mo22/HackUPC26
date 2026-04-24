import os
import pandas as pd
import random
from dataclasses import dataclass

CASES = ["Case0", "Case1", "Case2", "Case3"]

ITERATIONS = 800
INITIAL_ADDS = 80
MAX_POINTS_ADD = 80
MAX_POINTS_REINSERT = 120

random.seed(42)


@dataclass
class BayType:
    id: int
    w: int
    d: int
    h: int
    gap: int
    loads: int
    price: int


@dataclass
class PlacedBay:
    id: int
    x: int
    y: int
    w: int
    d: int
    h: int
    gap: int
    rot: int
    price: int
    loads: int


def safe_read_csv(path, columns):
    try:
        df = pd.read_csv(path, header=None, skipinitialspace=True)
        df.columns = columns
        return df.astype(int)
    except pd.errors.EmptyDataError:
        return pd.DataFrame(columns=columns).astype(int)


def read_case(case_dir):
    wh = safe_read_csv(os.path.join(case_dir, "warehouse.csv"), ["x", "y"])
    obs = safe_read_csv(os.path.join(case_dir, "obstacles.csv"), ["x", "y", "w", "d"])
    ceiling = safe_read_csv(os.path.join(case_dir, "ceiling.csv"), ["x", "h"])
    bays = safe_read_csv(os.path.join(case_dir, "types_of_bays.csv"), ["id", "w", "d", "h", "gap", "loads", "price"])

    poly = [(int(r.x), int(r.y)) for _, r in wh.iterrows()]
    types = [BayType(*map(int, row)) for row in bays.values]

    return poly, obs, ceiling, types


def overlap(a, b):
    ax, ay, aw, ad = a
    bx, by, bw, bd = b

    return not (
        ax + aw <= bx or
        bx + bw <= ax or
        ay + ad <= by or
        by + bd <= ay
    )


def vertical_intervals_at_x(poly, x):
    ys = []

    for i in range(len(poly)):
        x1, y1 = poly[i]
        x2, y2 = poly[(i + 1) % len(poly)]

        if y1 == y2:
            xmin, xmax = sorted((x1, x2))
            if xmin < x < xmax:
                ys.append(y1)

    ys.sort()

    intervals = []
    for i in range(0, len(ys) - 1, 2):
        intervals.append((ys[i], ys[i + 1]))

    return intervals


def rect_inside_polygon(rect, poly):
    x, y, w, d = rect

    if w <= 0 or d <= 0:
        return False

    x1 = x
    x2 = x + w
    y1 = y
    y2 = y + d

    xs = {x1, x2}

    for px, _ in poly:
        if x1 < px < x2:
            xs.add(px)

    xs = sorted(xs)

    sample_xs = []
    for a, b in zip(xs, xs[1:]):
        if a < b:
            sample_xs.append((a + b) / 2)

    if not sample_xs:
        sample_xs = [(x1 + x2) / 2]

    for sx in sample_xs:
        intervals = vertical_intervals_at_x(poly, sx)

        ok = False
        for iy1, iy2 in intervals:
            if y1 >= iy1 and y2 <= iy2:
                ok = True
                break

        if not ok:
            return False

    return True


def min_ceiling_between(x1, x2, ceiling):
    ceiling = ceiling.sort_values("x").reset_index(drop=True)
    result = float("inf")

    for i in range(len(ceiling)):
        cx = int(ceiling.loc[i, "x"])
        h = int(ceiling.loc[i, "h"])

        if i + 1 < len(ceiling):
            nx = int(ceiling.loc[i + 1, "x"])
        else:
            nx = float("inf")

        if max(x1, cx) < min(x2, nx):
            result = min(result, h)

    return result


def bay_rect(p):
    return (p.x, p.y, p.w, p.d)


def gap_rect(p):
    if p.gap <= 0:
        return None

    if p.rot == 0:
        return (p.x, p.y + p.d, p.w, p.gap)
    else:
        return (p.x + p.w, p.y, p.gap, p.d)


def all_rects(p):
    rects = [bay_rect(p)]
    gr = gap_rect(p)

    if gr is not None:
        rects.append(gr)

    return rects


def candidate_rects(x, y, w, d, gap, rot):
    main = (x, y, w, d)

    if gap <= 0:
        return [main]

    if rot == 0:
        gr = (x, y + d, w, gap)
    else:
        gr = (x + w, y, gap, d)

    return [main, gr]


def valid_candidate(x, y, w, d, h, gap, rot, sol, obs, poly, ceiling, ignore_idx=None):
    rects = candidate_rects(x, y, w, d, gap, rot)

    for r in rects:
        if not rect_inside_polygon(r, poly):
            return False

        for o in obs.itertuples(index=False):
            obs_rect = (int(o.x), int(o.y), int(o.w), int(o.d))
            if overlap(r, obs_rect):
                return False

        for i, p in enumerate(sol):
            if ignore_idx is not None and i == ignore_idx:
                continue

            for other in all_rects(p):
                if overlap(r, other):
                    return False

    if h > min_ceiling_between(x, x + w, ceiling):
        return False

    return True


def score(sol):
    area = sum(p.w * p.d for p in sol)
    loads = sum(p.loads for p in sol)
    price = sum(p.price for p in sol)

    return area + 50000 * loads - price


def bay_score(t):
    reserved_area = t.w * (t.d + t.gap)
    return (t.w * t.d * t.loads) / max(1, t.price * reserved_area)


def candidate_points(poly, obs, sol):
    pts = set()

    for p in poly:
        pts.add(p)

    pts.add((0, 0))

    for o in obs.itertuples(index=False):
        x = int(o.x)
        y = int(o.y)
        w = int(o.w)
        d = int(o.d)

        pts.add((x, y))
        pts.add((x + w, y))
        pts.add((x, y + d))
        pts.add((x + w, y + d))

    for p in sol:
        for x, y, w, d in all_rects(p):
            pts.add((x, y))
            pts.add((x + w, y))
            pts.add((x, y + d))
            pts.add((x + w, y + d))

    return list(pts)


def add_bay(sol, types, poly, obs, ceiling):
    points = candidate_points(poly, obs, sol)
    random.shuffle(points)

    type_list = sorted(types, key=bay_score, reverse=True)

    best = None
    best_value = -1

    for x, y in points[:MAX_POINTS_ADD]:
        for t in type_list:
            for rot in [0, 1]:
                if rot == 0:
                    w, d = t.w, t.d
                else:
                    w, d = t.d, t.w

                if valid_candidate(x, y, w, d, t.h, t.gap, rot, sol, obs, poly, ceiling):
                    candidate = PlacedBay(
                        t.id, int(x), int(y), w, d,
                        t.h, t.gap, rot, t.price, t.loads
                    )

                    value = score(sol + [candidate])

                    if value > best_value:
                        best_value = value
                        best = candidate

    if best is not None:
        sol.append(best)

    return sol


def move(sol, poly, obs, ceiling):
    if not sol:
        return sol

    i = random.randrange(len(sol))
    p = sol[i]

    step = random.choice([50, 100, 200, 400])

    nx = p.x + random.choice([-step, 0, step])
    ny = p.y + random.choice([-step, 0, step])

    if valid_candidate(nx, ny, p.w, p.d, p.h, p.gap, p.rot, sol, obs, poly, ceiling, ignore_idx=i):
        p.x = int(nx)
        p.y = int(ny)

    return sol


def rotate(sol, poly, obs, ceiling):
    if not sol:
        return sol

    i = random.randrange(len(sol))
    p = sol[i]

    nw = p.d
    nd = p.w
    nr = 1 - p.rot

    if valid_candidate(p.x, p.y, nw, nd, p.h, p.gap, nr, sol, obs, poly, ceiling, ignore_idx=i):
        p.w = nw
        p.d = nd
        p.rot = nr

    return sol


def compact(sol, poly, obs, ceiling):
    order = sorted(range(len(sol)), key=lambda i: (sol[i].y, sol[i].x))

    for i in order:
        p = sol[i]

        changed = True

        while changed:
            changed = False

            for dx, dy in [(-50, 0), (0, -50)]:
                nx = p.x + dx
                ny = p.y + dy

                if valid_candidate(nx, ny, p.w, p.d, p.h, p.gap, p.rot, sol, obs, poly, ceiling, ignore_idx=i):
                    p.x = int(nx)
                    p.y = int(ny)
                    changed = True

    return sol


def reinsert(sol, poly, obs, ceiling):
    if not sol:
        return sol

    i = random.randrange(len(sol))
    p = sol.pop(i)

    points = candidate_points(poly, obs, sol)
    random.shuffle(points)

    best_pos = None

    for x, y in points[:MAX_POINTS_REINSERT]:
        if valid_candidate(x, y, p.w, p.d, p.h, p.gap, p.rot, sol, obs, poly, ceiling):
            best_pos = (int(x), int(y))
            break

    if best_pos is not None:
        p.x, p.y = best_pos

    sol.append(p)

    return sol


def remove_and_refill(sol, types, poly, obs, ceiling):
    if not sol:
        return sol

    sol.pop(random.randrange(len(sol)))

    for _ in range(3):
        sol = add_bay(sol, types, poly, obs, ceiling)

    return sol


def clone(sol):
    return [PlacedBay(**p.__dict__) for p in sol]


def is_valid_solution(sol, obs, poly, ceiling):
    for i, p in enumerate(sol):
        if not valid_candidate(
            p.x, p.y, p.w, p.d,
            p.h, p.gap, p.rot,
            sol, obs, poly, ceiling,
            ignore_idx=i
        ):
            print("Invalid bay:", i, p)
            return False

    return True


def hill(sol, types, poly, obs, ceiling):
    best = clone(sol)
    best_score = score(best)

    for it in range(ITERATIONS):
        new = clone(best)

        op = random.choice([
            add_bay,
            move,
            rotate,
            compact,
            reinsert,
            remove_and_refill
        ])

        if op in [add_bay, remove_and_refill]:
            new = op(new, types, poly, obs, ceiling)
        else:
            new = op(new, poly, obs, ceiling)

        s = score(new)

        if s > best_score and is_valid_solution(new, obs, poly, ceiling):
            best = new
            best_score = s

    return best


def solve_case(case_dir):
    print(f"\n=== Solving {case_dir} ===")

    poly, obs, ceiling, types = read_case(case_dir)

    sol = []

    print("Construyendo solución inicial...")

    for _ in range(INITIAL_ADDS):
        sol = add_bay(sol, types, poly, obs, ceiling)

    print("Inicial:", len(sol), "bays")

    print("Optimizando...")
    sol = hill(sol, types, poly, obs, ceiling)

    valid = is_valid_solution(sol, obs, poly, ceiling)

    out = []
    for p in sol:
        out.append([p.id, p.x, p.y, p.rot])

    output_path = os.path.join(case_dir, "solution.csv")

    pd.DataFrame(out, columns=["Id", "X", "Y", "Rotation"]).to_csv(output_path, index=False)

    print("Final:", len(sol), "bays")
    print("Score:", score(sol))
    print("Valid:", valid)
    print("Written:", output_path)


def main():
    for case in CASES:
        if os.path.isdir(case):
            solve_case(case)
        else:
            print(f"Skipping {case}: folder not found")


if __name__ == "__main__":
    main()