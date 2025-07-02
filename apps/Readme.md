# Introduction

This folder contains code examples of how to use the stack.

The intention of the examples is to explain certain aspects of the stack.
e.g. provide information in how to build an KNX IoT Point API device based on the stack.

# Example Applications

* The *.c files are windows/linux console applications. 
* The *.cpp files are windows gui applications, with several table views and interaction buttons. 
  They 'include' the corresponding *.c files frome above for the data defintion. 

## EITT Applications

### Folder '/eitt'

Used to pass the stack certification with KNX EITT tool.

- **knx_iot_virtual_eitt.c** 
	- the endpoints and its types are defined as the EITT test templates requesting it (see in file)

- **knx_iot_virtual_eitt.cpp** 
	

## ETS Applications 

ETS demo applications, used to test the stack with KNX ETS6 tool.

### Folder '/lsab' and '/lssb'

Contains the *.c and *.cpp code files for the Light Switch Actuator Basic (LSAB) and Light Switch Sensor Basic (LSSB). 

- **knx_iot_virtual_lsab.c** and **knx_iot_virtual_lssb.c**
- **knx_iot_virtual_lsab.cpp** and **knx_iot_virtual_lssb.cpp**

> The above defined ETS applications supports only their intended datapoints, eg; for the sensor application onyl sensor datapoints. 
  If (for example) you enable for a sensor in ETS also the actuator functionaliy and assign to the actuator objects also GA's, 
  the ETS download of sensor application will fail for the virtaul sensor device (this demo bevaior may be improved in the future). 

> Note that the current stack does not work properly on sending a separate LSAB status per button from the 
  LSAB GUI application. This is under investigation.


### Folder '/knxtools'

Contains a (preregisterd) ETS6 **product** and a (predefined) ETS6 **project**.  

- **knx_iot_virtual_lsxb.knxprod** (product)
- **knx_iot_virtual_lsxb.knxproj** (project)

More details on how to use/edit the product and/or project in ETS6, for this see in [ETS6 pages](../../wikis/Home/ETS6). 	