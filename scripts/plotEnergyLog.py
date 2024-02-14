#!/usr/bin/env python3

import pandas
import matplotlib.pyplot as plt
import os

scriptDir = os.path.dirname(__file__)
outputDir = os.path.normpath(os.path.join(scriptDir, '..', 'output'))
powerLogFile = os.path.join(outputDir, 'power_log.csv')

def plotPowerLog():
	df = pandas.read_csv(powerLogFile)
	
	plot = df.plot('time', 'power')
	plot.set_ylim(ymin=0)
	plt.show()

plotPowerLog()