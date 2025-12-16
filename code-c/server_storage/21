import sys
import os
library_path = os.path.join(os.path.dirname(__file__), '.', 'Library')
sys.path.append(library_path)
library_path = os.path.join(os.path.dirname(__file__), '.', 'Algorithm')
sys.path.append(library_path)
from maze import maze
import DFS
import BFS
import aStar
import dijkstra

import csv
import random
import time

num_runs = 100

results_node = []
results_time = []
results_path = []

for _ in range(num_runs):
    row = 50
    col = 50
    m = maze(row, col)
    x = random.randint(1, row)
    y = random.randint(1, col)
    start = (x, y)
    endX = random.randint(1, row)
    endY = random.randint(1, col)
    m.CreateMaze(endX, endY, loopPercent=10.0)

    # DFS
    start_time = time.time()
    dSearch, dfsPath, dfwdPath = DFS.DFS(m, start)
    end_time = time.time()
    DFS_time = end_time - start_time

    # BFS
    start_time = time.time()
    bSearch, bfsPath, bfwdPath = BFS.BFS(m, start)
    end_time = time.time()
    BFS_time = end_time - start_time

    # AStar
    start_time = time.time()
    aSearch, aPath, afwdPath = aStar.aStar(m, start)
    end_time = time.time()
    AStar_time = end_time - start_time

    # Dijkstra
    start_time = time.time()
    path, cost, cell = dijkstra.dijkstra(m,start)
    end_time = time.time()
    Dijkstra_time = end_time - start_time

    results_node.append([len(dSearch), len(bSearch), len(aSearch), cell])
    results_time.append([DFS_time, BFS_time, AStar_time, Dijkstra_time])
    results_path.append([len(dfwdPath), len(bfwdPath), len(afwdPath), len(path)])

headers = ['DFS', 'BFS', 'AStar', 'Dijkstra']
with open('./data/node.csv', 'w', newline='') as file:
    writer = csv.writer(file)
    writer.writerow(headers)  
    writer.writerows(results_node)

with open('./data/time.csv', 'w', newline='') as file:
    writer = csv.writer(file)
    writer.writerow(headers)  
    writer.writerows(results_time)

with open('./data/path.csv', 'w', newline='') as file:
    writer = csv.writer(file)
    writer.writerow(headers)  
    writer.writerows(results_path)