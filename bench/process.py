import glob
import argparse
import os
import sys

import numpy as np
import matplotlib.pyplot as plt
import pandas as pd
import matplotlib.ticker as ticker

parser = argparse.ArgumentParser(
                    prog = 'process',
                    description = 'Process dguard benchmark data')
parser.add_argument('directories', type=str, help="Directories containing .results files", nargs='+')


args = parser.parse_args()
paths = args.directories

if(len(paths) == 1):
    print("Multiple directories please", file=sys.stderr)
    exit()

cwd = os.getcwd()
path_results = []

for path in paths:
        os.chdir(cwd)
        path = path.rstrip("/")
        os.chdir(path+"/results")

        files = glob.glob('*.results')
        benchnames = ['.'.join(x.split('.')[:-1]) for x in files]
        results = {}

        if(len(files) == 0):
            print("Directory "+path+" has no result files!", file=sys.stderr)
            exit()

        for i,fname in enumerate(files):
                benchname = benchnames[i]
                results[benchname] = []

                with open(fname, "r") as file:
                    lines = file.readlines()
                    for line in lines:
                        line = line.strip()
                        nums = line.split('m')
                        runtime = (float(nums[0]) * 60.0) + float(nums[1].replace(',', '.'))
                        results[benchname].append(runtime)
        path_results.append(results)



for results in path_results[1:]:
    for benchname in benchnames:
        results[benchname] = np.average(results[benchname]) / np.average(path_results[0][benchname])

for benchname in benchnames:
    path_results[0][benchname] =  1.
        
pandas_data = {}

for i,results in enumerate(path_results):
    data = []
    for benchname in benchnames:
        data.append(results[benchname])
    pandas_data[paths[i].rstrip("/")] = data


df = pd.DataFrame(pandas_data, index=benchnames)
print(df)

df.plot.bar()
ax = plt.gca()
ax.set_axisbelow(True)
ax.grid(axis='y')
plt.yscale("log")

ax.set_yticks([1.0, 10.0 ,50.0])
ax.get_yaxis().set_major_formatter(ticker.FormatStrFormatter("%.1f"))

ax.yaxis.set_minor_formatter(ticker.FormatStrFormatter("%.1f"))
ax.tick_params(which="both", axis='y', labelsize=6.5)
ax.grid(axis="y", visible=True, which='minor', linestyle='--')
plt.xticks(rotation=45, ha='right')

os.chdir(cwd)
plt.savefig("bench.svg", format="svg", bbox_inches="tight")
