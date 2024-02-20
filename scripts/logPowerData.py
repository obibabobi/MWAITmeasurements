#!/usr/bin/env python3

import subprocess
from timeit import default_timer as timer
import sys, os, signal
import statistics
import getopt
import csv
from decimal import Decimal

smartdroidPath = os.environ.get('SMARTDROID_PATH', './smartdroid')
if not os.path.isfile(smartdroidPath):
	print("To use the ODROID Smart Power, the 'smartdroid' tool is required.\n"
		"Please supply the path of the smartdroid binary by setting the SMARTDROID_PATH environment variable!")
	exit(1)

scriptDir = os.path.dirname(__file__)

"""
Python does not automatically raise a KeyboardInterrupt on a SIGINT in all cases.
Since we need it to exit the log loop, we explicitly register the required handler here.
"""
def sigint_handler(signal, frame):
	raise KeyboardInterrupt
signal.signal(signal.SIGINT, sigint_handler)


def getPower():
	result = subprocess.run([smartdroidPath, "-m power"],
			capture_output = True,
			text = True)
	return float(result.stdout)

def getNextValue(startValue):
	while True:
		nextValue = getPower()
		if nextValue != startValue:
			return nextValue


"""
This function will repeatedly read the power value from the Smart Power measurement device
and calculate how long it takes for it to change.
As this time has been observed to have quite a lot of outliers, it will repeat this calculation 
100 times and then return the median of the collected periods.
"""
def getMeasurementPeriod():
	# wait for begin of new measurement period
	powerValue = getNextValue(getPower())

	observedPeriods = []
	startTimer = timer()
	for _ in range(0,100):
		powerValue = getNextValue(powerValue)
		endTimer = timer()
		observedPeriods.append(endTimer - startTimer)
		startTimer = endTimer
	
	# if you want more information on the distribution of the periods 
	# that occured during the measurment, you can print the deciles
	# by uncommenting this line
	#print(statistics.quantiles(observedPeriods, n=10))
	
	return statistics.median(observedPeriods)


def logToFile(log):
	outputDir = os.path.normpath(os.path.join(scriptDir, '..', 'output'))
	csvFile = os.path.join(outputDir, 'power_log.csv')

	with open(csvFile, 'w') as file:
		writer = csv.writer(file)

		keys = list(log.keys())
		writer.writerow(keys)
		for i in range(0, len(log[keys[0]])):
			writer.writerow([log[key][i] for key in keys])


def logPower():
	log = {}
	log['time'] = []
	log['power'] = []

	powerValue = 0.0
	try:
		while True:
			powerValue = getNextValue(powerValue)
			log['time'].append(timer())
			log['power'].append(powerValue)
	except KeyboardInterrupt:
		pass

	timeOffset = log['time'][0]
	for i in range(0, len(log['time'])):
		log['time'][i] -= timeOffset
	
	# limit time precision to milliseconds
	for i in range(0, len(log['time'])):
		log['time'][i] = round(Decimal(log['time'][i]), 3)

	logToFile(log)


def main():
	if os.geteuid() != 0:
		print("To read any values from the ODROID Smart Power, root privileges are required. Please run as root!")
		exit(1)

	_, args = getopt.getopt(sys.argv[1:], '')
	if 'period' in args:
		period = getMeasurementPeriod()
		print(period)
		exit(0)
	
	logPower()
	exit(0)

main()