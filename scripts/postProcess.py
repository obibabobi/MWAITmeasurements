#!/usr/bin/env python3

import pandas as pd
import os, sys
from dataclasses import dataclass
from decimal import Decimal
import csv
import statistics

scriptDir = os.path.dirname(__file__)
outputDir = os.path.normpath(os.path.join(scriptDir, '..', 'output'))
resultsDir = os.path.join(outputDir, 'results')

signalTimesFile = os.path.join(resultsDir, 'signal_times')
durationFile = os.path.join(resultsDir, 'duration')
powerLogFile = os.path.join(outputDir, 'power_log.csv')

# read duration and convert it from milliseconds to seconds
duration = pd.read_csv(durationFile, names=['duration'])['duration'][0]
duration /= 1000

def decimalConverter(value):
	return Decimal(value)

powerLog = None


@dataclass
class PowerIntervall:
	min: float
	max: float
	isRising: bool

	def __contains__(self, value):
		return value >= self.min and value <= self.max
	
	def below(self, value):
		return self.min <= value
	
	def above(self, value):
		return self.max >= value

def getPowerIntervall(time):
	i = 0
	nextTime = powerLog['time'][0]
	while nextTime <= time:
		i += 1
		nextTime = powerLog['time'][i]
	powerValues = [ powerLog['power'][i-1], powerLog['power'][i] ]
	return PowerIntervall(min(powerValues), max(powerValues), (powerValues[1] - powerValues[0]) >= 0)
		

@dataclass
class PowerPattern:
	pattern: list
	period: Decimal

	def __len__(self):
		return len(self.pattern)

	def fitsSequence(self, sequence):
		if len(sequence) is not len(self)+1:
			return False 
		
		risingStartEdge = self.pattern[0] > 0
		startValue = sequence[0].min if risingStartEdge else sequence[0].max
		for i in range(0, len(self.pattern)):
			risingEdge = self.pattern[i] > 0
			if risingEdge:
				value = sequence[i].min + startValue * self.pattern[i]
				if not sequence[i+1].above(value):
					return False
			else:
				value = sequence[i].max + startValue * self.pattern[i]
				if not sequence[i+1].below(value):
					return False
		return True

def generatePattern(edgeCount, threshold):
	pattern = []
	sign = -1
	for _ in range(0, edgeCount):
		pattern.append(sign * threshold)
		sign *= -1
	return pattern

powerPattern = PowerPattern(generatePattern(3, Decimal(0.1)), Decimal(duration))


@dataclass
class ApproximateTime:
	time: Decimal
	error: Decimal

	def __rsub__(self, other):
		return ApproximateTime(other - self.time, self.error)

	def __le__(self, other):
		return self.time + self.error <= other
	
	def __ge__(self, other):
		return self.time - self.error >= other

def getNextTime(time):
	nextTime = powerLog['time'][0]
	i = 1
	while nextTime <= time and i < len(powerLog):
		nextTime = powerLog['time'][i]
		i += 1
	return nextTime

def seekPattern():
	startTime = powerLog['time'][0]
	sequence = [ 0 ] * (len(powerPattern)+1)
	
	while startTime + (len(sequence)-1) * powerPattern.period < powerLog['time'][len(powerLog)-1]:
		for i in range(0, len(sequence)):
			sequence[i] = getPowerIntervall(startTime + i * powerPattern.period)
		timeStep = 1 << 20	# some big number to work down from
		for i in range(0, len(sequence)):
			time = startTime + i * powerPattern.period
			stepSize = getNextTime(time) - time
			if stepSize < timeStep:
				timeStep = stepSize
		if powerPattern.fitsSequence(sequence):
			return ApproximateTime(startTime+timeStep/2, timeStep/2)
		startTime += timeStep
	
	return None


def nSecToSeconds(nanoSeconds):
	return nanoSeconds / 1000000000

def getPowerValues(measurementDir, measureStartTime):
	startTimeFile = os.path.join(measurementDir, 'start_time')
	endTimeFile = os.path.join(measurementDir, 'end_time')
	startTimes = pd.read_csv(startTimeFile, names=['end_time'])['end_time']
	endTimes = pd.read_csv(endTimeFile, names=['end_time'])['end_time']

	for i in range(0, len(startTimes)):
		startTimes[i] = nSecToSeconds(startTimes[i]) - measureStartTime
		endTimes[i] = nSecToSeconds(endTimes[i]) - measureStartTime

	powerValues = [ [] for _ in range(len(startTimes)) ]
	i = 0
	while i < len(powerLog['time']):
		time = powerLog['time'][i]
		for j in range(0, len(startTimes)):
			if startTimes[j] <= time <= endTimes[j]:
				powerValues[j].append(powerLog['power'][i])
		i += 1

	return powerValues


def writePowerValues(measurementDir, powerValues):
	powerFile = os.path.join(measurementDir, 'power')

	with open(powerFile, 'w') as file:
		writer = csv.writer(file)

		for i in range(0, len(powerValues)):
			writer.writerow([powerValues[i]])


def associateExternalMeasurements():
	global powerLog
	powerLog = pd.read_csv(powerLogFile, converters={'time': decimalConverter, 'power': decimalConverter})

	logStartTime = seekPattern()
	powerLog['time'] -= logStartTime

	signalTimes = pd.read_csv(signalTimesFile, names=['signal_times'])['signal_times']
	measureStartTime = nSecToSeconds(signalTimes[0])

	measurementTypes = [ e.name for e in os.scandir(resultsDir) if e.is_dir() ]
	for mType in measurementTypes:
		typeDir = os.path.join(resultsDir, mType)
		measurementNames = [ e.name for e in os.scandir(typeDir) if e.is_dir() ]
		for mName in measurementNames:
			measurementDir = os.path.join(typeDir, mName)

			powerValues = getPowerValues(measurementDir, measureStartTime)
			for i in range(len(powerValues)):
				if powerValues[i]:
					powerValues[i] = statistics.mean(powerValues[i])
					powerValues[i] = round(Decimal(powerValues[i]), 3)
				else:
					print('No values found for ' + mType + ':' + mName + '[' + str(i) + ']', file=sys.stderr)
					powerValues[i] = -1

			writePowerValues(measurementDir, powerValues)


def toJoule(point1MicroJoule):
	return point1MicroJoule / 10000000

def evaluateInternalMeasurements():
	measurementTypes = [ e.name for e in os.scandir(resultsDir) if e.is_dir() ]
	for mType in measurementTypes:
		typeDir = os.path.join(resultsDir, mType)
		measurementNames = [ e.name for e in os.scandir(typeDir) if e.is_dir() ]
		for mName in measurementNames:
			measurementDir = os.path.join(typeDir, mName)

			energyFile = os.path.join(measurementDir, 'energy_consumption')
			energyValues = pd.read_csv(energyFile, names=['energy'])['energy']

			powerValues = toJoule(energyValues) / duration

			for i in range(0, len(powerValues)):
				powerValues[i] = round(Decimal(powerValues[i]), 5)

			writePowerValues(measurementDir, powerValues)


def main():
	if os.path.isfile(signalTimesFile):
		associateExternalMeasurements()
	else:
		evaluateInternalMeasurements()


main()
