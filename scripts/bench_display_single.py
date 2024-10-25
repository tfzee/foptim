import os
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt


def show_plot_single(filename):
    data = pd.read_csv(filename)

    colors = [("red" if x.endswith("baseline") else "green") for x in data["command"]]
    
    plt.bar(data["command"], data["mean"], color = colors)
    plt.show()


def show_plot_comp(filename, filename2):
    data = pd.read_csv(filename)
    data2 = pd.read_csv(filename2)

    colors = [("red" if x.endswith("baseline") else "green") for x in data["command"]]
    
    plt.bar(data["command"], np.array(data2["mean"])/np.array(data["mean"]), color = colors)
    plt.show()





if __name__ == "__main__":
    show_plot_single("../test/Output/perf.csv")
    show_plot_single("../test/Output/compile.csv")
    # show_plot_comp("../test/Output/compile.old.csv", "../test/Output/compile.csv")
