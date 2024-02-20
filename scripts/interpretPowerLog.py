#!/usr/bin/env python3

import pandas as pd
import os
from dataclasses import dataclass
from decimal import Decimal

scriptDir = os.path.dirname(__file__)
outputDir = os.path.normpath(os.path.join(scriptDir, '..', 'output'))
resultsDir = os.path.join(outputDir, 'results')

signalTimesFile = os.path.join(resultsDir, 'signal_times')
powerLogFile = os.path.join(outputDir, 'power_log.csv')


def decimalConverter(value):
	return Decimal(value)

powerLog = pd.read_csv(powerLogFile, converters={'time': decimalConverter, 'power': decimalConverter})


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
	period: float

	def __len__(self):
		return len(self.pattern)

	def fitsSequence(self, sequence):
		if len(sequence) is not len(self)+1:
			return False 
		
		risingStartEdge = self.pattern[0] > 0
		startValue = sequence[0].min if risingStartEdge else sequence[0].max
		modifier = Decimal(1.0)
		for i in range(0, len(self.pattern)):
			modifier += self.pattern[i]
			value = startValue * modifier
			risingEdge = self.pattern[i] > 0
			if ((risingEdge and not sequence[i+1].above(value)) or 
			(not risingEdge and not sequence[i+1].below(value))):
				return False
		return True

def generatePattern(flankCount, threshold):
	pattern = []
	sign = -1
	for _ in range(0, flankCount):
		pattern.append(sign * threshold)
		sign *= -1
	return pattern

powerPattern = PowerPattern(generatePattern(3, Decimal(0.1)), Decimal(1))


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
		if powerPattern.fitsSequence(sequence):
			return startTime
		timeStep = 1 << 20	# some big number to work down from
		for i in range(0, len(sequence)):
			time = startTime + i * powerPattern.period
			stepSize = getNextTime(time) - time
			if stepSize < timeStep:
				timeStep = stepSize
		startTime += timeStep
	
	return None


def main():
	print(seekPattern())


main()
