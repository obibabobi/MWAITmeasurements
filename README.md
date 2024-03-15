# DISCLAIMER

CAUTION! THIS IS A PROTOTYPE! DO NOT USE IN PRODUCTIVE SYSTEMS!
This framework interacts directly with some hardware the Linux kernel uses as well.
Side effects are not unlikely, leading to potential negative consequences like data loss and system instability!

# Dependencies

If you want to use a ODROID Smart Power for gathering external energy measurements, the command line tool ```smartdroid``` is required.
It can be found [here](https://github.com/obibabobi/SmartDroid).
Thanks to [@mhaehnel](https://github.com/mhaehnel) for providing this tool!

# Prior Remarks

This guide's goal is to enable you to utilize this measurement framework on your machines.
That being said, it is still in development and has so far not been tested on a lot of different systems.
Experience has shown that some tinkering is likely necessary when trying new machines.

So far, this framework can be used with x86 and ARM.
It has been mainly tested with the 6.2.9 version of the Linux kernel.


# General Setup

Using the measurement framework in its current form requires 2 steps to be taken, namely:

1. Deploying and compiling the kernel module
2. Measuring

These steps will be described in more detail further into this document.

The way the framework was used so far is to have 2 machines, one that is the one to be measured (aka ```measurebox```),
and another one that is in charge of controlling the measuring process and evaluating the results (aka ```controllbox```).
Technically you can do all that on one machine, but this approach has advantages when measuring multiple machines and for development.
Since this guide as well as the utilities are geared towards splitting these tasks between two systems, it is recommended that you do to.
If you want to use the provided scripts, the systems need to be able to reach each other over IPv4.


# Setup Steps

## 1. Deploy and compile the kernel module

In this step, the files necessary for compiling the kernel module and executing a measurement run need to be transferred from the ```controllbox``` to the ```measurebox```.
These files can be found in the ```mwait_deploy``` folder.
Afterwards, the kernel module needs to be compiled on the ```measurebox``` in preparation for its usage.

The ```deploy.sh``` script was created to facilitate this process and can be used like this:
```console
user@controllbox:~/MWAITmeasurements# ./deploy.sh <IPv4 of the measurebox>
```


## 2. Measure

The last step is to use the compiled kernel module to take measurements.
For this step again a script (```measure.sh```) exists, which can be used as follows:
```console
user@controllbox:~/MWAITmeasurements# ./measure.sh <IPv4 of the measurebox> <duration>
```

```duration``` defines how long a single measurement should last. This is mainly dependent on your source of energy values.
For example, x86 RAPL counts the energy used by the processor in a register, updating it every millisecond.
A measurement duration of 100 ms has mostly been used in this case.

When using external measurements with the ODROID Smart Power, the ```-e``` flag should be given.
This flag starts the ```scripts/logPowerData.py``` script to run in the background on the ```controllbox``` during the measurement, collecting power values.
Additionally, it instructs the ```measurebox``` to generate an energy pattern and timing information that is used later for synchronizing the collected power data with the measurement times.
A duration of 1000 ms has proven to be appropriate for this specific measuring device.

Once the measurements on the ```measurebox``` are done, the script copies the results from the ```measurebox``` to the ```output``` folder on the ```controllbox```.
As a last step, it calls ```scripts/postProcess.py``` to do some necessary evaluation and ```scripts/plotMeasurements.py``` to generate some simple visualizations into the ```output``` folder.


# Further Notes

## Using other external measurement devices

For external measurements, at this point in time, only the ODROID Smart Power has been used as a measuring device.
Because of this ```measure.sh``` is geared towards this device, calling the ```scripts/logPowerData.py``` script which uses the [SmartDroid](https://github.com/obibabobi/SmartDroid) tool to collect energy values into a log.
However, if you want to use another measuring device, not many adjustments should be necessary.
All you need is some source of timestamped energy values.
The support for external measurements in the kernel module and the other scripts is completely agnostic towards the source of this energy log (provided it is in the correct format).
If you adjust the ```measure.sh``` script to not call ```scripts/logPowerData.py``` but provide your own power log in ```output/power_log.csv```, everything *should* work.

## Configuring the measurements

Which specific circumstances should be measured during a measurement run can be configured in the ```mwait_deploy/measure.sh``` script.
By default, the idle states used by the cpuidle driver are measured, as well as each combination of hardware threads sleeping / doing a simple workload.

New measurements are added by calling the ```measure``` function.
This function's parameters are the name of the specific measurement, then the parameters to be used when inserting the kernel module and finally the name of the folder to put the results in.
For information on the available parameters of the kernel module, please execute ```modinfo``` on the compiled module.
