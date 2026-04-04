kRows, kCols = 32, 23
grid = [["#" for _ in range(kCols)] for _ in range(kRows)]
for c in range(9, 14):
    grid[0][c] = " "
# Row 1: 11 -> 1
for c in range(1, 12):
    grid[1][c] = " "
# Even rows 2..30: single connector
for r in range(2, 31, 2):
    if (r // 2) % 2 == 1:
        grid[r][1] = " "
    else:
        grid[r][21] = " "
# Odd rows 3..29: alternating full horizontal runs
for r in range(3, 30, 2):
    if ((r - 3) // 2) % 2 == 0:
        for c in range(1, 22):
            grid[r][c] = " "
    else:
        for c in range(21, 0, -1):
            grid[r][c] = " "
# North row 31: connect from (30,1) and open gate cols 9-13
grid[31][1] = " "
for c in range(2, 12):
    grid[31][c] = " "
for c in range(9, 14):
    grid[31][c] = " "

from collections import deque

def count_open():
    return sum(row.count(" ") for row in grid)

open_cells = count_open()
start = None
for r in range(kRows):
    for c in range(kCols):
        if grid[r][c] == " ":
            start = (r, c)
            break
    if start:
        break
q = deque([start])
seen = {start}
while q:
    r, c = q.popleft()
    for dr, dc in ((-1, 0), (1, 0), (0, -1), (0, 1)):
        nr, nc = r + dr, c + dc
        if 0 <= nr < kRows and 0 <= nc < kCols and grid[nr][nc] == " " and (nr, nc) not in seen:
            seen.add((nr, nc))
            q.append((nr, nc))
print("reachable", len(seen), "open", open_cells, "ok" if len(seen) == open_cells else "BAD")
for row in grid:
    print('    "' + "".join(row) + '",')
