import glob
import argparse
import os
import sys

import numpy as np
import matplotlib.pyplot as plt
import pandas as pd
import seaborn as sns

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
for opt in range(0,3):
    path_results = []

    for path in paths:
        os.chdir(cwd)
        path = path.rstrip("/")
        os.chdir(path+"/results")

        files = glob.glob('*_O{}.results'.format(opt))
        benchnames = ['.'.join(x.split('.')[:-1]) for x in files]
        results = {}

        if(len(files) == 0):
            print("Directory "+path+" has no result O{} files!".format(opt), file=sys.stderr)
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
   # print(path_results)       
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
    plt.xticks(rotation=45, ha='right')

    os.chdir(cwd)
    plt.savefig("bench_O{}.svg".format(opt), format="svg")
