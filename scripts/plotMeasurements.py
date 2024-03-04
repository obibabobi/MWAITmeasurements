#!/usr/bin/env python3

import os
import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.ticker as mtick
import re

scriptDir = os.path.dirname(__file__)
outputDir = os.path.normpath(os.path.join(scriptDir, '..', 'output'))
resultsDir = os.path.join(outputDir, 'results')


def sortingFunction(index):
    numbers = re.findall(r'\d+', index)
    if len(numbers)==0:
        return 0
    splitByNr = re.split(r'\d+', index)
    return int(numbers[0]) * 2 + (splitByNr[1]!='')


def addPkgAttribute(df, dir, measurementName, fileName):
    newDf = pd.read_csv(os.path.join(dir, measurementName, fileName), names=[measurementName])
    df.insert(len(df.columns), measurementName, newDf[measurementName])

def plotPkgMeasurements(dirName, fileName, divisor = None):
    dir = os.path.join(resultsDir, dirName)
    df = pd.DataFrame()
    for measurementName in os.listdir(dir):
        addPkgAttribute(df, dir, measurementName, fileName)
    if divisor is not None:
        df /= divisor
    df = df.reindex(sorted(df.columns, key=sortingFunction), axis=1)
    return df.plot.box()


totalTscFileName = 'total_tsc'

def addPkgCstates(means, index, cstates, dir, measurementName):
    measurementPath = os.path.join(dir, measurementName)

    index.append(measurementName)

    series = {}
    series[totalTscFileName] = pd.read_csv(os.path.join(measurementPath, totalTscFileName), header=None).iloc[:,0]
    for state in cstates:
        series[state] = pd.read_csv(os.path.join(measurementPath, state), header=None).iloc[:,0]

    series['unspecified'] = series[totalTscFileName]
    for state in cstates:
        series['unspecified'] = series['unspecified'] - series[state]
    
    means['unspecified'].append((series['unspecified']/series[totalTscFileName]).mean())
    for state in cstates:
        means[state].append((series[state]/series[totalTscFileName]).mean())

def getCoreCount(dir):
    i = 0
    while True:
        cpuDirName = 'cpu'+str(i)
        if not os.path.exists(os.path.join(dir, cpuDirName)):
            break
        i += 1
    return i

def calculateCoreAverage(dir, state, coreCount, unspecified):
    series = None
    for i in range(0, coreCount):
        data = pd.read_csv(os.path.join(dir, 'cpu'+str(i), state), header=None).iloc[:,0]
        if series is None:
            series = data
        else:
            series += data
        unspecified[i] = unspecified[i] - data
    
    return series / coreCount

def addCoreCstates(means, index, cstates, dir, measurementName):
    measurementPath = os.path.join(dir, measurementName)
    coreCount = getCoreCount(measurementPath)

    index.append(measurementName)

    series = {}
    series[totalTscFileName] = pd.read_csv(os.path.join(measurementPath, totalTscFileName), header=None).iloc[:,0]
    unspecified = []
    for i in range(0, coreCount):
        unspecified.append(series[totalTscFileName])
    for state in cstates:
        series[state] = calculateCoreAverage(measurementPath, state, coreCount, unspecified)

    series['unspecified'] = unspecified[0]
    for i in range(1, coreCount):
        series['unspecified'] += unspecified[i]
    series['unspecified'] /= coreCount

    means['unspecified'].append((series['unspecified']/series[totalTscFileName]).mean())
    for state in cstates:
        means[state].append((series[state]/series[totalTscFileName]).mean())

def plotResidencies(dirName, cstates, gatherDataFunction):
    dir = os.path.join(resultsDir, dirName)
    means = {}
    means['unspecified'] = []
    for state in cstates:
        means[state] = []
    index = []
    for measurementName in os.listdir(dir):
        gatherDataFunction(means, index, cstates, dir, measurementName)
    df = pd.DataFrame(means, index=index)
    df = df.reindex(sorted(index, key=sortingFunction))
    return df.plot.bar(stacked=True)


def main():
    statesDirName = 'states'
    powerFileName = 'power'

    try:
        plot = plotPkgMeasurements(statesDirName, powerFileName)
        plot.set_ylim(ymin=0)
        plot.set_ylabel('Watts')
        plot.figure.savefig(os.path.join(outputDir, powerFileName + '_by_' + statesDirName + '.pdf'))
    except FileNotFoundError:
        pass

    cpusDirName = 'cpus_sleep'

    try:
        plot = plotPkgMeasurements(cpusDirName, powerFileName)
        plot.set_ylim(ymin=0)
        plot.set_ylabel("Watts")
        plot.figure.savefig(os.path.join(outputDir, powerFileName + '_by_' + cpusDirName + '.pdf'))
    except FileNotFoundError:
        pass

    wakeupTimeFileName = 'wakeup_time'
    wakeupTimePath = os.path.join('cpu0', wakeupTimeFileName)
    wakeupTimeDivisor = 1000    # nanoecond to microsecond

    try:
        plot = plotPkgMeasurements(statesDirName, wakeupTimePath, wakeupTimeDivisor)
        plot.set_ylim(ymin=0)
        plot.set_ylabel("microseconds")
        plot.figure.savefig(os.path.join(outputDir, wakeupTimeFileName + '_by_' + statesDirName + '.pdf'))
    except FileNotFoundError:
        pass


    pkgCstates = [ 'pkg_c2', 'pkg_c3', 'pkg_c6', 'pkg_c7' ]
    try:
        plot = plotResidencies(statesDirName, pkgCstates, addPkgCstates)
        plot.yaxis.set_major_formatter(mtick.PercentFormatter(1.0))
        plot.figure.savefig(os.path.join(outputDir, 'pkg_residencies_by_' + statesDirName + '.pdf'))
    except FileNotFoundError:
        pass

    coreCstates = [ 'unhalted', 'c3', 'c6', 'c7' ]
    try:
        plot = plotResidencies(statesDirName, coreCstates, addCoreCstates)
        plot.yaxis.set_major_formatter(mtick.PercentFormatter(1.0))
        plot.figure.savefig(os.path.join(outputDir, 'core_residencies_by_' + statesDirName + '.pdf'))
    except FileNotFoundError:
        pass


main()
