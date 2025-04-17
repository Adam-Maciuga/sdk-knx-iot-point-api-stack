# Introduction

This folder contains code examples of how to use the stack.

The intention of the examples is to explain certain aspects of the stack.
e.g. provide information in how to build an KNX IoT Point API device based on the stack.

# Example applications

## Serial numbers/ passwords

Can be set in code file. 
Note, the example applications have deliberated incorrect serial numbers.

## EITT Applications 

Used to pass the stack certification with KNX EITT tool.

- **knx_iot_virtual_eitt.c** 
	- a windows/linux console application 
	- the endpoints and its types are defined as EITT test templates requesting it (see in file)

- **knx_iot_virtual_eitt.cpp** 
	- a windows gui application
	- with several table views and interaction buttons

## ETS Applications 

Used to test the stack with KNX ETS6 tool.

- **knx_iot_virtual_lsxb.c** 
	- a windows/linux console application 
	- the endpoints and its types are defined according to a LSAB and LSSB (see in file)
		- LSAB (Light Switch Actuator Basic), Functional Block ID 417 with endpoints's (EPs) switch on off (soo) + info on off (ioo)
		- LSAB (Light Switch Sensor Basic), Functional Block ID 421 with endpoints's (EPs) switch on off (soo) + info on off (ioo)

- **knx_iot_virtual_lsxb.cpp** 
	- a windows gui application
	- with several table views and interaction buttons
	
- **knx_iot_virtual_lsxb.knxprod** 
	- a (pregistered) ETS6 **product** (catalog entry), to be imported in the ETS6 catalog 
	- the product correlates 1:1 with the application code from above, especially with the number and type of of EPs 
	- how to use it in ETS, see in [ETS6 pages](../../wikis/Home/ETS6)
	- use it when you want to embedd the product in existing ETS projects
	
	**knx_iot_virtual_lsxb.knxproj** 
	- a (predefined) ETS6 **project** including two products from above, to be imported in the ETS6 dashboard
	- how to use it in ETS, see in [ETS6 pages](../../wikis/Home/ETS6)
		