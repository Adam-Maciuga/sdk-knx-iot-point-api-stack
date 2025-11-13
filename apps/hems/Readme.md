# Introduction

This folder contains the Energy Management System (EMS) demo apps. 

The use case is to charge an e-car with a power of 4 kW DC, moreover a user can decide 
for two operation modes.

- In **Sun** mode the e-car is charged exclusively with 'green' solar energy, if the available solar 
  power drops below the threshold of 4 kW (e.g.; cloudy weather) the charging of the e-car is paused.
- In **Mix** mode the charging will never be stopped unless the e-car is fully charged. 
  Hence the charged energy is a mix of solar energy and grid energy. In worst case all energy is 
  retrieved from the grid (e.g.; during night), in best case all energy is 'green' solar energy.   

The charging mode can be set by the user on the Customer Energy Manager (CEM).The below picture 
illustrates the use case.

![Concept](concept.png)

The following functionalities are involved in this demo use case. 

1. Inverter (provides solar energy)
2. Charger (consumes energy)
3. Customer Energy Manager (manages energy) 

> Note that the grid energy as such is not modelled in this demo. For simplification it is assumed 
  that grid energy is (always) available. 

In a typical installation the above defined three functionalities can be shared amongst several (end) devices.
For example, the CEM can be a standalone device or the functionality is part of the 'Charger' (end) device. 
In a usual installation with a stationary battery or heat pump the CEM is often part in one of those devices.

# References
All applications uses KNX standardized datapoints, the corresponding __Functional Block__ and 
__Datapoint Type__ definitions you can find in folder 'apps/hems/data'

a) 07_80 Introduction 
b) 07_80_01 Photovoltaics 
c) 07_80_03 eMoblity 
d) 03_07_02 Datapoint Types 

# 1. Inverter

The inverter demo represents an own end device with the photovoltaics functionality. 

> The demo implements the 'PowerDC' datapoint from the Functional Block (b), reflecting the
  sun beam radiation. It is represented by a slider, which allows the user to 'simulate'
  the present DC power from 0 to 10 kW in steps of 1 kW.

# 2. Charger

The charger demo represents an own end device with the charging functionality. 

> The demo implements the 'ActivePowerLimit' datapoint from the Functional Block (c), reflecting the
  charging consumption from the e-car. It is represented by a moving bar, which allows to 'simulate'
  the charging process. 

# 3. Customer Energy Manager

The customer energy manager demo represents an own end device with the the cem functionality. 

> The demo implements the (input) counterpart for the (output) 'PowerDC' datapoint from the Functional Block (b),
  the (output) counterpart for the (input) 'ActivePowerLimit' datapoint from the Functional Block (c), 
  and the (sun/mix) operation mode setting.

# Details 

## Datapoints

All above for the runtime relevant mentioned i/o datapoints are of the same type, being DPT 14.056, 
for this see 03/07/02. 

## Commissioning

This demo show a specific EMS functionality at runtime on end devices. In KNX IoT several commissioning 
methods exist to establish a runtime communication between devices. For this a client (tool) needs to 
support them.  

- __S-Mode__ (devices communicate over a fix endpoint, with group addresses)
- __Publish/Subscribe__ (devices communicate over one or more endpoints, with CoAP subscription and notification
                         mechanism [CoAP RFC7641](https://www.rfc-editor.org/rfc/rfc7641.html)) 

Both commissioning procedures are described as part of the KNX IoT specification 03/10/05. 
Client 'ETS' has implemented the S-Mode.
 
### S-Mode

The following commissioning steps are defined. For a (simple) installation of only a few devices some steps may not 
be needed (marked). 

1. Device Discovery 
   a) add/scan in client the target device certificate (such as a QR Code) 
   b) resolve device IPv6 address and port by serial number (CoAP multicast/mDNS discovery)
   c) retrieve device functional block information (CoAP unicast discovery) 
2. Device Preparation 
   a) initial onboarding via device certificate (well-known/knx/spake, SPAKE2+) 
   b) check and set individual address (well-known/knx/ia)
   c) check and reset device programming mode (dev/pm) -> optional
   d) check manufacturer id (dev/mid)
   e) check hardware type (dev/hwt)
   f) reset target device (well-known/knx) -> optional
3. Device Download
   a) check and set the load state machine (a/lsm)
   b) write Group Object table entries
   c) write Recipient table entries
   d) write Publisher table entries
   e) write Access Token entries
   f) write parameter values -> optional 
   g) check and close the load state machine (a/lsm)
4. Device Finishing
   a) read fingerprint and store in client (well-known/knx/f) -> optional
   b) restart device (well-known/knx) -> optional 

In case of commissioning with ETS a catalog entry is need for a device, to be created by means
of the KNX Manufacturer Tool. 

#### Group Addresses

In a simple installation usually only one device is present for a specific functionality, for example in this demo 
a single inverter functionality (and device). The inverter 'PowerDC' output datapoint maps 1:1 to the counterpart CEM 
input datapoint. Hence this, a user assignment of group addresses is not needed, a commissioning client can assign them 
by own means. For more complex installations/scenarios such as to handle two independent charger devices by the CEM, 
for this a user interaction is needed. This is not considered by this demo.  

#### Individual Addresses (IA)/ Serial Numbers (SN) / Installation ID (IID)

In this demo used SN's are arbitrary. 
The IA's and the IID depends on the commissioning client (step 2.b). 

### Objects and Tables

The Group Object are commissioned with step 3.b from above, Recipient and Publisher Tables
with step 3.c/d from above. To be known, important runtime i/o datapoint information are
described as follows: 
  
**Inverter**
-  url: '/p/inverter' (output, transmits solar power value), 
   datapoint type IEEE 754 single float (KNX datapoint type 14.056)  
   
**Customer Energy Manager**
-  url: '/p/inverter' (input, receives solar power value),
   datapoint type IEEE 754 single float (KNX datapoint type 14.056)

-  url: '/p/charger' (output, transmits charger value),
   datapoint type IEEE 754 single float (KNX datapoint type 14.056) 
  
**Charger**
-  url: '/p/charger' (input, receives charger value),
   datapoint type IEEE 754 single float (KNX datapoint type 14.056)
