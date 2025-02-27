import os
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt


def show_plot_single(filename):
    data = pd.read_csv(filename)

    names = list(data["command"])
    values = list(data["mean"])

    for i in range(0, len(values), 4):
        values[i+1] = values[i+1] / values[i+0]
        values[i+2] = values[i+2] / values[i+0]
        values[i+3] = values[i+3] / values[i+0]
        values[i+0] = values[i+0] / values[i+0]
    
    colors = [("red" if x.endswith("baseline") else "green") for x in names]

    plt.xticks(rotation=30, ha='right')
    plt.bar(names, values, color = colors)
    plt.show()


def show_plot_comp(filename, filename2):
    data = pd.read_csv(filename)
    data2 = pd.read_csv(filename2)

    colors = [("red" if x.endswith("baseline") else "green") for x in data["command"]]
    
    plt.bar(data["command"], np.array(data["mean"])/np.array(data2["mean"]), color = colors)
    plt.show()





if __name__ == "__main__":
    show_plot_single("../bench/Output/perf.csv")
    # show_plot_single("../bench/Output/compile.csv")
    show_plot_comp("../bench/Output/perf.csv", "../bench/Output/perf_25_02_27.csv")

