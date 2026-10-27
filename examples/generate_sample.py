#!/usr/bin/env python3
"""Generate synthetic sales data for trying out csvq.

Usage: python3 examples/generate_sample.py ROWS > out.csv
All data is randomly generated (fixed seed) - no real customers or companies.
"""
import csv
import random
import sys
from datetime import date, timedelta

def main() -> None:
    rows = int(sys.argv[1]) if len(sys.argv) > 1 else 200
    rng = random.Random(42)
    regions = ["North", "South", "East", "West"]
    products = {"Widget": 12.5, "Gadget": 30.0, "Gizmo": 15.0, "Doohickey": 7.25, "Sprocket": 4.4}
    reps = ["Asha", "Ben", "Chen", "Diego", "Elif", "Farah"]
    start = date(2026, 1, 1)

    w = csv.writer(sys.stdout, lineterminator="\n")
    w.writerow(["order_id", "date", "region", "rep", "product", "units", "unit_price", "revenue"])
    for i in range(1, rows + 1):
        product = rng.choice(list(products))
        price = products[product]
        units = rng.randint(1, 40)
        w.writerow([
            f"ORD-{i:05d}",
            (start + timedelta(days=rng.randrange(270))).isoformat(),
            rng.choice(regions),
            rng.choice(reps),
            product,
            units,
            f"{price:.2f}",
            f"{units * price:.2f}",
        ])

if __name__ == "__main__":
    main()
