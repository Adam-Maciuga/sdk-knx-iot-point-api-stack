from glob import glob
import logging
import json
from xmlrpc.client import SERVER_ERROR
import jsonpatch
import inspect
import socket
import ipaddress
import base64
import hashlib
import os.path
from cryptography.x509 import NameOID
from OpenSSL import crypto
from cryptography.hazmat.primitives import hashes
import binascii
import os
import sys
import re

from urllib.parse import urlparse

#------------------------------------------------------
# Global Variable Table
#
# Contains variables specified the conformance test XML#
# The variable values will be set / read during telegram
# sequence evaluation
#------------------------------------------------------

_PointApiVariables = {}



def GetScriptVersion():
	return "6"

#------------------------------------------------------
# Logging
#------------------------------------------------------

def InitializeLogging(strPath):
	logging.basicConfig(filename=strPath, format='%(asctime)s: %(levelname)s:%(message)s', encoding='utf-8', level=logging.DEBUG)
	return True


#------------------------------------------------------
# Error handling
#------------------------------------------------------

_script_last_error = ""

def ResetLastError():
	global _script_last_error
	_script_last_error = ""
	return True

def GetLastError():
	global _script_last_error
	logging.debug("GetLastError: " + _script_last_error)
	return _script_last_error

#------------------------------------------------------
# Message Constants
#------------------------------------------------------
NO_SRV_RECORD_RECEIVED = "No SRV record was received."
NO_AAAA_RECORD_RECEIVED = "No AAAA record was received."
DNS_SERVICE_ERROR = "Service error."
DNS_PLANNED_AND_RECEIVED_PORTS_DIFFER = "Planned and received ports differ."
DNS_PLANNED_AND_RECEIVED_HOST_DIFFER = "Planned and received host differ."
DNS_NO_PORT_RECEIVED = "The record contains no port."
VALUE_HAS_INVALID_FORMAT = "The value '{}' has an invalid format."

#------------------------------------------------------
# Helper Functions 
#------------------------------------------------------

# Called by EITT
def SetVariableTableEntry(strKey,strValue):
	_PointApiVariables[strKey] = strValue
	logging.debug(get_fn() + ', Key: ' + strKey + ', Value: ' + strValue + "(POINT API)")

	#_PointApiVariables["sequence_loop_limit.Copy of IA PREPARATION V17"] = "5"

	return "OK"

# Called by EITT
def GetVariableTableEntry(strKey):
	try:
		logging.debug(get_fn() + ', Key: ' + strKey + ', Value: ' + _PointApiVariables[strKey] + "(POINT API)")

		return _PointApiVariables[strKey]
	except BaseException as e:
		exc_type, exc_obj, exc_tb = sys.exc_info()
		error_msg = f'{get_fn()}, {type(e).__name__} in line {exc_tb.tb_lineno} occurred: {str(e)}'
		_script_last_error = error_msg
		logging.debug(error_msg)
		return ""

# Called by EITT
def ClearVariableTable(strValue):
	global _PointApiVariables
	_PointApiVariables = {}
	logging.debug("ClearVariableTable...")
	return ""


# Called internally by Python script functions
def _get_variable_table_entry(strKey):
	global _script_last_error
	try:
		return _PointApiVariables[strKey]

	except LookupError as e:
		_script_last_error = 'Variable not found: ' + strKey
		logging.debug(_script_last_error)
		raise LookupError

def get_fn():
    return inspect.stack()[1][3]

#------------------------------------------------------
# calculate fingerprint (SHA1, SHA256 for PEM files)

BEGIN_PEM_MARK = b"-----BEGIN CERTIFICATE-----"
END_PEM_MARK = b"-----END CERTIFICATE-----"

def get_fingerprint(strPemPath,strFingerprintType):
	global _script_last_error

	logging.debug("Get PEM file fingerprint: " + str(strPemPath))
	
	if not os.path.exists(strPemPath):	
		_script_last_error = "PEM file does not exist: " + strPemPath
		logging.debug(_script_last_error)
		return ""
	
	# read given PEM file into lines array
	arLines = []
	try:
		with open(strPemPath, "rb") as pemFile:
			for line in pemFile:
				if line.rstrip() == BEGIN_PEM_MARK:
					break
			for line in pemFile:
				if line.rstrip() == END_PEM_MARK:
					break
				arLines.append(line.strip())
			if not arLines:
				_script_last_error = "Invalid PEM file: " + strPemPath
				logging.debug(_script_last_error)
				return ""

	except BaseException as e:
		error_msg = f'{get_fn()}, {type(e).__name__} occurred: {str(e)}'
		_script_last_error = error_msg
		logging.debug(error_msg)
		return ""

	# decode PEM file
	arDER = base64.decodebytes(b''.join(arLines))
	# logging.debug("Fingerprint DER: " + str(arDER))

	# calculate desired hash
	arSHA = []
	if (strFingerprintType == "1") :
		arSHA = hashlib.sha1(arDER).digest()
	elif (strFingerprintType == "256") :
		arSHA = hashlib.sha256(arDER).digest()

	# format fingerprint (hex format xx:xx:xx:... in capital letters)
	strFingerprint = ":".join(format(x,'02X') for x in arSHA)
	logging.debug("Fingerprint SHA" + strFingerprintType + ": " + strFingerprint)
	return strFingerprint


#------------------------------------------------------
# Body evaluation test
#------------------------------------------------------
		

def eval_pointapi_test(planned, response):
	global _script_last_error
	try:
		logging.debug("eval_pointapi_test")

		# converts the planned paramater to JSON data
		# j_value = json.loads(planned)
		if (-1 != response.find(planned)) :
			return False

		_script_last_error = "'" + planned + "' not found"
		return False

	except BaseException as e:
		error_msg = f'{get_fn()}, {type(e).__name__} occurred: {str(e)}'
		_script_last_error = error_msg
		logging.debug(error_msg)
		return False

original_fingerprint = -1
original_fingerprint_step3 = -1
original_fingerprint_step5 = -1
original_fingerprint_step7 = -1
original_fingerprint_step9 = -1



def eval_5_2_4_1_step_1(planned, response):
	global _script_last_error
	global original_fingerprint
	global original_fingerprint_step3
	global original_fingerprint_step5
	global original_fingerprint_step7
	global original_fingerprint_step9
	try:
		logging.debug(get_fn())
		j_response = json.loads(response)
		original_fingerprint = j_response["value"]
		
		original_fingerprint_step3 = -1
		original_fingerprint_step5 = -1
		original_fingerprint_step7 = -1
		original_fingerprint_step9 = -1

		return True

	except BaseException as e:
		error_msg = f'{get_fn()}, {type(e).__name__} occurred: {str(e)}'
		_script_last_error = error_msg
		#logging.debug(error_msg)
		return False

def eval_5_2_4_1_step_3(planned, response):
	global _script_last_error
	global original_fingerprint
	global original_fingerprint_step3
	try:
		logging.debug(get_fn())		
		j_response = json.loads(response)
		original_fingerprint_step3 = j_response["value"]

		if (
			original_fingerprint_step3 == -1 or 
			original_fingerprint == -1
			):
			_script_last_error = f"Some fingerprints have no value. Check previous steps.\nStep 1: {original_fingerprint}\nStep 3: {original_fingerprint_step3}"
			return False
		if original_fingerprint_step3 == original_fingerprint:
			_script_last_error = f"Fingerprints are equal.\nStep 1: {original_fingerprint}\nStep 3: {original_fingerprint_step3}"
			return False # if fingerprints are the same, test should fail
		else:
			return True

	except BaseException as e:
		error_msg = f'{get_fn()}, {type(e).__name__} occurred: {str(e)}'
		_script_last_error = error_msg
		logging.debug(error_msg)
		return False

def eval_5_2_4_1_step_5(planned, response):
	global _script_last_error
	global original_fingerprint
	global original_fingerprint_step3
	global original_fingerprint_step5
	try:
		logging.debug(get_fn())
		
		j_response = json.loads(response)
		original_fingerprint_step5 = j_response["value"]
		
		if (
			original_fingerprint_step5 == -1 or 
			original_fingerprint_step3 == -1 or 
			original_fingerprint == -1
			):
			_script_last_error = f"Some fingerprints have no value. Check previous steps.\nStep 1: {original_fingerprint}\nStep 3: {original_fingerprint_step3}\nStep 5: {original_fingerprint_step5}"
			return False
		if (
			original_fingerprint_step5 == original_fingerprint or 
			original_fingerprint_step5 == original_fingerprint_step3
		   ):
			_script_last_error = f"Fingerprints are equal.\nStep 1: {original_fingerprint}\nStep 3: {original_fingerprint_step3}\nStep 5: {original_fingerprint_step5}"
			return False # if fingerprints are the same, test should fail
		else:
			return True

	except BaseException as e:
		error_msg = f'{get_fn()}, {type(e).__name__} occurred: {str(e)}'
		_script_last_error = error_msg
		logging.debug(error_msg)
		return False

def eval_5_2_4_1_step_7(planned, response):
	global _script_last_error
	global original_fingerprint
	global original_fingerprint_step3
	global original_fingerprint_step5
	global original_fingerprint_step7
	try:
		logging.debug(get_fn())
		
		j_response = json.loads(response)
		original_fingerprint_step7 = j_response["value"]
	
		if (
			original_fingerprint_step7 == -1 or 
			original_fingerprint_step5 == -1 or 
			original_fingerprint_step3 == -1 or 
			original_fingerprint == -1
			):
			_script_last_error = f"Some fingerprints have no value. Check previous steps.\nStep 1: {original_fingerprint}\nStep 3: {original_fingerprint_step3}\nStep 5: {original_fingerprint_step5}\nStep 7: {original_fingerprint_step7}"
			return False
		if (
			original_fingerprint_step7 == original_fingerprint or 
			original_fingerprint_step7 == original_fingerprint_step3 or 
			original_fingerprint_step7 == original_fingerprint_step5
		   ):
			_script_last_error = f"Fingerprints are equal.\nStep 1: {original_fingerprint}\nStep 3: {original_fingerprint_step3}\nStep 5: {original_fingerprint_step5}\nStep 7: {original_fingerprint_step7}"
			return False # if fingerprints are the same, test should fail
		else:
			return True

	except BaseException as e:
		error_msg = f'{get_fn()}, {type(e).__name__} occurred: {str(e)}'
		_script_last_error = error_msg
		logging.debug(error_msg)
		return False

def eval_5_2_4_1_step_9(planned, response):
	global _script_last_error
	global original_fingerprint
	global original_fingerprint_step3
	global original_fingerprint_step5
	global original_fingerprint_step7
	global original_fingerprint_step9
	try:
		logging.debug(get_fn())

		j_response = json.loads(response)
		original_fingerprint_step9 = j_response["value"]
		
		# logging.debug(original_fingerprint)
		# logging.debug(original_fingerprint_step3)
		# logging.debug(original_fingerprint_step5)
		# logging.debug(original_fingerprint_step7)
		# logging.debug(original_fingerprint_step9)
		if (
			original_fingerprint_step9 == -1 or 
			original_fingerprint_step7 == -1 or 
			original_fingerprint_step5 == -1 or 
			original_fingerprint_step3 == -1 or 
			original_fingerprint == -1
			):
			_script_last_error = f"Some fingerprints have no value. Check previous steps.\nStep 1: {original_fingerprint}\nStep 3: {original_fingerprint_step3}\nStep 5: {original_fingerprint_step5}\nStep 7: {original_fingerprint_step7}\nStep 9: {original_fingerprint_step9}"
			return False
		if (
			original_fingerprint_step9 == original_fingerprint or 
			original_fingerprint_step9 == original_fingerprint_step3 or 
			original_fingerprint_step9 == original_fingerprint_step5 or
			original_fingerprint_step9 == original_fingerprint_step7
		   ):
			_script_last_error = f"Fingerprints are equal.\nStep 1: {original_fingerprint}\nStep 3: {original_fingerprint_step3}\nStep 5: {original_fingerprint_step5}\nStep 7: {original_fingerprint_step7}\nStep 9: {original_fingerprint_step9}"
			return False # if fingerprints are the same, test should fail
		else:
			return True

	except BaseException as e:
		error_msg = f'{get_fn()}, {type(e).__name__} occurred: {str(e)}'
		_script_last_error = error_msg
		logging.debug(error_msg)
		return False
	
def set_original_fingerprint(planned, response):
	global _script_last_error
	global original_fingerprint
	try:
		logging.debug(get_fn())
		j_response = json.loads(response)
		original_fingerprint = j_response["value"]
		return True

	except BaseException as e:
		error_msg = f'{get_fn()}, {type(e).__name__} occurred: {str(e)}'
		_script_last_error = error_msg
		logging.debug(error_msg)
		return False

def eval_compare_with_original_fingerprint(planned, response):
	global _script_last_error
	global original_fingerprint
	try:
		j_response = json.loads(response)
		if j_response["value"] == original_fingerprint:
			return True
		else:
			_script_last_error = f"Fingerprints are not equal. Original fingerprint: {original_fingerprint}"
			return False

	except BaseException as e:
		error_msg = f'{get_fn()}, {type(e).__name__} occurred: {str(e)}'
		_script_last_error = error_msg
		logging.debug(error_msg)
		return False
	
	
mdns_ip = ""
mdns_port = ""
	
def pointapi_eval_5_1_2_1_get_port(response):
	global mdns_port
	global _script_last_error
	try:
		logging.debug("pointapi_eval_5_1_2_1_get_port: " + str(response))
		# converts the server response to JSON data
		j_value = json.loads(response)

		logging.debug("eval_5811a_server_port")
		logging.debug(str(j_value))

		if j_value["dns"]["received"]["type"] != "SRV":
			logging.debug("No SRV record")
			_script_last_error = NO_SRV_RECORD_RECEIVED
			return False
		if j_value["dns"]["planned"]["service"] != j_value["dns"]["received"]["service"].rstrip("."):
			logging.debug("SERVICE ERROR")
			_script_last_error = SERVER_ERROR 
			return False

		rcvPort = j_value["dns"]["received"]["port"]
		logging.debug(f"received port is: {rcvPort}")
		plannedPort = j_value["dns"]["planned"]["port"]
		logging.debug(f"planned port is: {plannedPort}")

		if (plannedPort != 0) :
			if (plannedPort != rcvPort) :
				logging.debug("planned and received ports differ")
				_script_last_error = DNS_PLANNED_AND_RECEIVED_PORTS_DIFFER 
				return False

		mdns_port = str(rcvPort)
		return True

	except BaseException as e:
		error_msg = f'{get_fn()}, {type(e).__name__} occurred: {str(e)}'
		_script_last_error = error_msg
		logging.debug(error_msg)
		return False

def pointapi_eval_5_1_2_1_get_address(response):
	global mdns_ip
	global _script_last_error
	try:
		# converts the server response to JSON data
		j_value = json.loads(response)

		logging.debug("pointapi_eval_5_1_2_1_get_address: " + str(response))
		logging.debug(str(j_value))

		if j_value["dns"]["received"]["type"] != "AAAA":
			logging.debug("No AAAA record")
			_script_last_error = NO_AAAA_RECORD_RECEIVED
			return False

		rcvIP = j_value["dns"]["received"]["host"]
		logging.debug(f"received IP is: {rcvIP}")

		mdns_ip = str(rcvIP)
		logging.debug("pointapi_eval_5_1_2_1_get_address, returns TRUE")
		return True

	except BaseException as e:
		error_msg = f'{get_fn()}, {type(e).__name__} occurred: {str(e)}'
		_script_last_error = error_msg
		logging.debug(error_msg)
		return False



def patch_address_5_1_2_1(device):
	global mdns_ip
	global mdns_port

	device_address = f"[{mdns_ip}]:{mdns_port}"
	logging.debug(f"patch_address_5_1_2_1: {device_address}")
	mdns_ip = ""
	mdns_port = "" # reset ip and port so the next test will not be affected
	return device_address



#------------------------------------------------------
# Software Update
#------------------------------------------------------
swu_file_bytes = bytes(0)
last_sent_byte_index = 0
last_sent_block_number = 0


def swu_patch_content(old_content):
	global swu_file_bytes
	global _script_last_error
	global last_sent_byte_index
	try:
		loop_count = int(GetVariableTableEntry("sequence_loop_count.Copy of Upload Block - First Half"))
		block_size = int(GetVariableTableEntry("PREP_BLOCK_SIZE"))
		file_size = len(swu_file_bytes)

		start_index = last_sent_byte_index + 1
		end_index = start_index + block_size

		if loop_count == 1:
			last_sent_byte_index = 0
			file_name = GetVariableTableEntry("PREP_UPDATE_FILE_PATH")

			swu_file_bytes = bytes(0)
			with open(file_name, "rb") as swu_file:
				logging.debug(f"Loading update file from: {file_name}")
				swu_file_bytes = swu_file.read()

			file_size = len(swu_file_bytes)
			loop_limit = int(file_size / block_size)
			loop_limit = int(loop_limit / 2)  
			SetVariableTableEntry("sequence_loop_limit.Copy of Upload Block - First Half", str(loop_limit))

			start_index = 0
			end_index = (start_index + block_size)



		if end_index > file_size:
			end_index = start_index + (file_size % block_size)

		last_sent_byte_index = end_index - 1

		logging.debug(f"swu_patch_content - Sending {str(end_index - start_index)} bytes (#{start_index} - #{end_index - 1} of {file_size})")

		
		hex_data = binascii.hexlify(swu_file_bytes[start_index:end_index]).decode('utf-8')
		string_data = ' '.join(hex_data[i:i+2] for i in range(0, len(hex_data), 2))

		logging.debug(string_data)
		return string_data
	except BaseException as e:
		exc_type, exc_obj, exc_tb = sys.exc_info()
		error_msg = f'{get_fn()}, {type(e).__name__} in line {exc_tb.tb_lineno} occurred: {str(e)}'
		_script_last_error = error_msg
		logging.debug(error_msg)
		return False

def swu_patch_parameter(old_parameter):
	global last_sent_block_number
	global swu_file_bytes

	try:
		loop_count = int(GetVariableTableEntry("sequence_loop_count.Copy of Upload Block - First Half"))
		block_size = int(GetVariableTableEntry("PREP_BLOCK_SIZE"))
		

		if loop_count == 1:
			file_name = GetVariableTableEntry("PREP_UPDATE_FILE_PATH")

			swu_file_bytes = bytes(0)
			with open(file_name, "rb") as swu_file:
				logging.debug(f"Loading update file from: {file_name}")
				swu_file_bytes = swu_file.read()

			file_size = len(swu_file_bytes)
			last_sent_block_number = 0
			return f"?ps={block_size}&pkgs={file_size}"
		
		else:
			last_sent_block_number += 1

			start_index = last_sent_byte_index + 1
			end_index = (start_index + block_size)
			file_size = len(swu_file_bytes)

			if end_index > file_size:
				end_index = start_index + (file_size % block_size)
		
			# "?KEY=VALUE&"
			return f"?po={last_sent_block_number * block_size}&ps={block_size}"
		
	except BaseException as e:
		exc_type, exc_obj, exc_tb = sys.exc_info()
		error_msg = f'{get_fn()}, {type(e).__name__} in line {exc_tb.tb_lineno} occurred: {str(e)}'
		_script_last_error = error_msg
		logging.debug(error_msg)
		return False




def swu_patch_content_continue(old_content):
	global swu_file_bytes
	global _script_last_error
	global last_sent_byte_index
	try:
		loop_count = int(GetVariableTableEntry("sequence_loop_count.Copy of Upload Block - Second Half"))
		block_size = int(GetVariableTableEntry("PREP_BLOCK_SIZE"))

		file_size = len(swu_file_bytes)
		remaining_bytes = file_size - (last_sent_byte_index + 1)

		if remaining_bytes > block_size:
			loop_limit = int(remaining_bytes / block_size) + loop_count
			logging.debug(f"Remaining bytes: {remaining_bytes} | loop_limit: {loop_limit}")

			if (remaining_bytes % block_size) == 0:
				loop_limit -= 1
				logging.debug(f"{remaining_bytes} % {block_size} = {remaining_bytes % block_size}| loop_limit: {loop_limit}")

			SetVariableTableEntry("sequence_loop_limit.Copy of Upload Block - Second Half", str(loop_limit))

		start_index = last_sent_byte_index + 1
		end_index = start_index + block_size

		if end_index > file_size:
			end_index = start_index + (file_size % block_size)

		last_sent_byte_index = end_index - 1

		logging.debug(f"swu_patch_content_continue - Sending {str((end_index - start_index))} bytes (#{start_index} - #{end_index - 1} of {file_size})")

		
		hex_data = binascii.hexlify(swu_file_bytes[start_index:end_index]).decode('utf-8')
		string_data = ' '.join(hex_data[i:i+2] for i in range(0, len(hex_data), 2))

		logging.debug(string_data)
		return string_data
	except BaseException as e:
		exc_type, exc_obj, exc_tb = sys.exc_info()
		error_msg = f'{get_fn()}, {type(e).__name__} in line {exc_tb.tb_lineno} occurred: {str(e)}'
		_script_last_error = error_msg
		logging.debug(error_msg)
		return False

def swu_patch_parameter_continue(old_parameter):
	global swu_file_bytes
	global last_sent_byte_index
	global last_sent_block_number

	last_sent_block_number += 1
	block_size = int(GetVariableTableEntry("PREP_BLOCK_SIZE"))

	# ps: block size
	# po: block number
 
	start_index = last_sent_byte_index + 1
	end_index = (start_index + block_size)

	file_size = len(swu_file_bytes)
	if end_index > file_size:
		end_index = start_index + (file_size % block_size)
 
	# "?KEY=VALUE&"
	return f"?po={last_sent_block_number * block_size}&ps={end_index - start_index}"


def eval_5_10_1_1_IPv6_adresses(planned, response):
	global _script_last_error
	try:
		logging.debug(get_fn())
		j_response = json.loads(response)
		
		regEx = re.compile('h\'[0-9a-fA-F]{32}\'')

		if isinstance(j_response, dict):
			# JSON response is an object
			matches = regEx.match(j_response["value"])

			if matches == None:
				_script_last_error = VALUE_HAS_INVALID_FORMAT.format(j_response["value"])
				return False

		if isinstance(j_response, list):
			# JSON response is an array
			for j_obj in j_response:
				matches = regEx.match(j_obj["value"])

				if matches == None:
					_script_last_error = VALUE_HAS_INVALID_FORMAT.format(j_obj["value"])
					return False		

		return True

	except BaseException as e:
		error_msg = f'{get_fn()}, {type(e).__name__} occurred: {str(e)}'
		_script_last_error = error_msg
		#logging.debug(error_msg)
		return False
		

		
#======================================================
# KNXNET/IP related functions
#======================================================
			
#------------------------------------------------------
# helper
#------------------------------------------------------

def Hex2Int(strHexValue: str) -> int:
    # remove spaces
    strValue = strHexValue.replace(" ", "")
    # convert to int
    return int(strValue, 16)

# def Int2Hex(nIntValue: int) -> str:
#     # convert int value to hex string
#     hex_str = format(nIntValue, "X")

#     # if the number of digits is odd, add a leading '0'
#     if len(hex_str) % 2 != 0:
#         hex_str = "0" + hex_str

#     # split into 2-digit groups and join with spaces
#     return " ".join(hex_str[i:i+2] for i in range(0, len(hex_str), 2))

def Int2Hex(nIntValue: int, hex_length: int = 0) -> str:
    # convert int value to hex string
    hex_str = format(nIntValue, "X")

    # if the number of digits is odd, add a leading '0'
    if len(hex_str) % 2 != 0:
        hex_str = "0" + hex_str

    # pad with leading zeros to reach the desired total byte length
    if hex_length > 0:
        total_digits = hex_length * 2
        hex_str = hex_str.zfill(total_digits)

    # split into 2-digit groups and join with spaces
    return " ".join(hex_str[i:i+2] for i in range(0, len(hex_str), 2))

#------------------------------------------------------
# Global Variable Table
#------------------------------------------------------

kip_ApiVariables = {}

#------------------------------------------------------
# Management functions
#------------------------------------------------------

def kip_get_fn():
    return inspect.stack()[1][3]

# Called by EITT
def kip_GetScriptVersion():
	return "1"

#------------------------------------------------------
# called by EITT, don't change
#------------------------------------------------------

# Called by EITT
def kip_eitt_get_variablecount(strParam):
	global kip_ApiVariables
	return str(len(kip_ApiVariables))

# Called by EITT
def kip_eitt_get_variable_by_position(strParam):
	global kip_ApiVariables
	try:
		# logging.debug(kip_get_fn() + ', strParam: ' + strParam)
		
		# create JSON from parameter string
		j_parameter = json.loads(strParam)
		
		# get variable name from json
		strPos = j_parameter["pos"]
		
		# set value to variable table
		strKey = list(kip_ApiVariables)[int(strPos)]
		strValue = list(kip_ApiVariables.values())[int(strPos)]
		
		# return result data as JSON string for evaluation in EITT
		return "{ \"key\": \"" + strKey + "\", \"value\" : \"" + strValue + "\"}"

	except BaseException as e:
		exc_type, exc_obj, exc_tb = sys.exc_info()
		error_msg = f'{kip_get_fn()}, {type(e).__name__} in line {exc_tb.tb_lineno} occurred: {str(e)}'
		_script_last_error = error_msg
		logging.debug(error_msg)
		return ""

# Called by EITT
def kip_SetVariableTableEntry(strKey,strValue):
	global kip_ApiVariables
	kip_ApiVariables[strKey] = strValue
	logging.debug(kip_get_fn() + ', Key: ' + strKey + ', Value: ' + strValue)
	return "OK"

# Called by EITT
def kip_GetVariableTableEntry(strKey):
	global kip_ApiVariables
	try:
		logging.debug(kip_get_fn() + ', Key: ' + strKey + ', Value: ' + kip_ApiVariables[strKey])
		return kip_ApiVariables[strKey]
		
	except BaseException as e:
		exc_type, exc_obj, exc_tb = sys.exc_info()
		error_msg = f'{kip_get_fn()}, {type(e).__name__} in line {exc_tb.tb_lineno} occurred: {str(e)}'
		_script_last_error = error_msg
		logging.debug(error_msg)
		return ""

# Called by EITT
def kip_ClearVariableTable(strValue):
	global kip_ApiVariables
	kip_ApiVariables = {}
	logging.debug(kip_get_fn() + "...")
	return ""

# Called by EITT
def kip_PrepareFn(strValue):
	global kip_ApiVariables
	logging.debug(kip_get_fn() + ', Input: ' + strValue + ', Output: ' + strValue)
	return strValue


#------------------------------------------------------
# user comment command functions
#------------------------------------------------------

#----------------------------
# sets the value of a variable in the Python variable table from a comment command
# creates the variable if not yet contained in the table
# returns an error, if an exception occurs

def kip_set_variable(strParam):
	global kip_ApiVariables
	try:
		logging.debug(kip_get_fn() + ', strParam: ' + strParam)
		
		# create JSON from parameter string
		j_parameter = json.loads(strParam)
		
		# get variable name from json
		strKey = j_parameter["key"]
		# get value from json
		strValue = j_parameter["value"]
		
		# set value to variable table
		if strValue =="LRCV":
			kip_ApiVariables[strKey] = kip_ApiVariables["RcvNotifyTimerMTag"]
			# return result data as JSON string for evaluation in EITT
			return "{ \"message\": \"'" + strKey + "' is set to " + str(Hex2Int(kip_ApiVariables["RcvNotifyTimerMTag"])) + "(" + hex(Hex2Int(kip_ApiVariables["RcvNotifyTimerMTag"])) + ")\", \"result\" : \"ok\"}"

		else:
			kip_ApiVariables[strKey] = strValue

			# return result data as JSON string for evaluation in EITT
			return "{ \"message\": \"'" + strKey + "' is set to " + str(Hex2Int(strValue)) + "(" + hex(Hex2Int(strValue)) + ")\", \"result\" : \"ok\"}"

	except BaseException as e:
		exc_type, exc_obj, exc_tb = sys.exc_info()
		error_msg = f'Python exception: {kip_get_fn()}, {type(e).__name__} in line {exc_tb.tb_lineno} occurred: {str(e)}'
		_script_last_error = error_msg
		logging.debug(error_msg)
		return "{ \"message\": \"'" + error_msg + "'\", \"result\" : \"af\"}"

#----------------------------
# for demonstration only, the functions creates always an error
# because the returned JSON result is 'af' 

def kip_create_error(strParam):
	global kip_ApiVariables
	try:
		logging.debug(kip_get_fn() + ', strParam: ' + strParam)
		
		# create JSON from parameter string
		j_parameter = json.loads(strParam)
		
		# get variable name from json
		strMessage = j_parameter["message"]
		
		# do anything
		
		# return result data as JSON string for evaluation in EITT
		return "{ \"message\": \"'" + strMessage + "'\", \"result\" : \"af\"}"

	except BaseException as e:
		exc_type, exc_obj, exc_tb = sys.exc_info()
		error_msg = f'Python exception: {kip_get_fn()}, {type(e).__name__} in line {exc_tb.tb_lineno} occurred: {str(e)}'
		_script_last_error = error_msg
		logging.debug(error_msg)
		return "{ \"message\": \"'" + error_msg + "'\", \"result\" : \"af\"}"

#----------------------------
# compares the last received message tag with the content 
# of the Python variable 'MyMessageTag'

def kip_check_messagetag(strParam):
	global kip_ApiVariables
	try:
		logging.debug(kip_get_fn() + ', strParam: ' + strParam)
		
		# create JSON from parameter string
		j_parameter = json.loads(strParam)
		# get variable name from json
		strParam1Value = j_parameter["param1"]
		
		# do anything
		strLastReceivedMessageTag = kip_ApiVariables["RcvRoutingMTag"]
		nLastReceivedMessageTag = Hex2Int(strLastReceivedMessageTag)

		strMyMessageTag = kip_ApiVariables["MyMessageTag"]
		nMyMessageTag = Hex2Int(strMyMessageTag)

		strMessage = "no message"
		strResult = "ok"

		if nLastReceivedMessageTag > nMyMessageTag:
			strResult = "af"
			strMessage = "received message tag too high (" + strLastReceivedMessageTag + " is greater than " + strMyMessageTag + ")"
		
		# return result data as JSON string for evaluation in EITT
		return "{ \"message\": \"'" + strMessage + "'\", \"result\" : \"" + strResult + "\"}"

	except BaseException as e:
		exc_type, exc_obj, exc_tb = sys.exc_info()
		error_msg = f'Python exception: {kip_get_fn()}, {type(e).__name__} in line {exc_tb.tb_lineno} occurred: {str(e)}'
		_script_last_error = error_msg
		logging.debug(error_msg)
		return "{ \"message\": \"'" + error_msg + "'\", \"result\" : \"af\"}"



def kip_70106(strParam):
	
	global kip_ApiVariables
	
	strMessage = "no"
	strResult = "ok"

	try:

		# return result data as JSON string for evaluation in EITT
		return "{ \"message\": \"'" + strMessage + "'\", \"result\" : \"" + strResult + "\"}"	

	except BaseException as e:
		exc_type, exc_obj, exc_tb = sys.exc_info()
		error_msg = f'Python exception: {kip_get_fn()}, {type(e).__name__} in line {exc_tb.tb_lineno} occurred: {str(e)}'
		script_last_error = error_msg
		logging.debug(error_msg)
		return "{ \"message\": \"'" + error_msg + "'\", \"result\" : \"af\"}"


def kip_test(strParam):

	global kip_ApiVariables
	
	try:
		# logging.debug(kip_get_fn() + ', strParam: ' + strParam)
		
		# create JSON from parameter string
		j_parameter = json.loads(strParam)
		# get variable name from json
		strTestNum = j_parameter["test_func"]
		
		strAutoNotiftTimerValue = kip_ApiVariables["AutoNotifyTimerValue"]
		nAutoNotiftTimerValue = Hex2Int(strAutoNotiftTimerValue)
		
		strMessage = "no"
		strResult = "ok"

		#------------------------------------------------------------------------------------------
		
		if strTestNum == "GlPro_SetupMulticas":

			strLastReceivedTimeStamp = kip_ApiVariables["RcvTimeStamp"]
			nLastReceivedTimeStamp = Hex2Int(strLastReceivedTimeStamp)

			if nLastReceivedTimeStamp > 10000000 :
				strMessage = "PYTHON EVALUATION--> Received timestamp is greater than 10.000.000     "
				strResult = "ok"

			else:
				strMessage = "PYTHON EVALUATION--> Received time-stamp is not greater than 10.000.000"
				strResult = "af"

		#------------------------------------------------------------------------------------------
    
		if strTestNum == "set_time_stamp_t1": 

			kip_ApiVariables["time_stamp_t1"] = kip_ApiVariables["RcvTimeStamp"]

			strMessage = "PYTHON VALUE SETTING--> time_stamp_t1 is set to RcvTimeStamp      t1: " + kip_ApiVariables["time_stamp_t1"] + ", AutoNotifyTimerValue:" + kip_ApiVariables["AutoNotifyTimerValue"]
			strResult = "ok"

		if strTestNum == "set_time_stamp_t1_IN": 

			kip_ApiVariables["time_stamp_t1"] = kip_ApiVariables["AutoNotifyTimerValue"]

			strMessage = "PYTHON VALUE SETTING--> time_stamp_t1 is set to AutoNotifyTimerValue    t1: " + kip_ApiVariables["time_stamp_t1"]  + ", AutoNotifyTimerValue:" + kip_ApiVariables["time_stamp_t1"]
			strResult = "ok"	


		#------------------------------------------------------------------------------------------

		if strTestNum == "print_local_mctimer":

			strMessage = "PYTHON VALUE PRINT--> Local MC_TIMER (hex): " +  kip_ApiVariables["AutoNotifyTimerValue"] + "  Local MC_TIMER (dec): " + str(nAutoNotiftTimerValue) 
			strResult = "ok"

		#------------------------------------------------------------------------------------------	

		if strTestNum == "print_local_systemtime":

			strAutoNotiftTimerTime = kip_ApiVariables["AutoNotifyTimerTime"]
			nAutoNotiftTimerTime = Hex2Int(strAutoNotiftTimerTime)/10000

			strMessage = "PYTHON VALUE PRINT--> Local SYSTEM_TIME (hex): " +  kip_ApiVariables["AutoNotifyTimerTime"] + "  Local SYSTEM_TIME (dec,ms): " + str(nAutoNotiftTimerTime) 
			strResult = "ok"	

		#------------------------------------------------------------------------------------------

		if strTestNum == "last_rcv _to_mc_timer":

			kip_ApiVariables["AutoNotifyTimerValue"] = kip_ApiVariables["RcvTimeStamp"]

			strMessage = "PYTHON VALUE SETTING--> AutoNotifyTimerValue is set to RcvTimeStamp"
			strResult = "ok"

		#------------------------------------------------------------------------------------------	

		if strTestNum == "mc_timer_shift":


			strMcShiftValue= j_parameter["mc_shift_value"]
			nMcShiftValue = int(strMcShiftValue)

			nNewTimeStamp = (nAutoNotiftTimerValue + nMcShiftValue)

			strNewTimeStamp= Int2Hex(nNewTimeStamp,6)

			kip_ApiVariables["NotUpdated_NTValue"]= kip_ApiVariables["AutoNotifyTimerValue"]
			kip_ApiVariables["AutoNotifyTimerValue"]= strNewTimeStamp 
			kip_ApiVariables["Updated_NTValue"]= strNewTimeStamp 
	
			strMessage = "PYTHON VALUE SETTING--> newtime stamp value is addjusted. NOT SHIFTED MC_TIMER:" + kip_ApiVariables["NotUpdated_NTValue"] +  "   SHIFTED MC_TIMER:" + kip_ApiVariables["Updated_NTValue"] +  "   AutoNotifyTimerValue:" + kip_ApiVariables["AutoNotifyTimerValue"]
			strResult = "ok"

		#---------------------------------------------------------------------------------------------
		
		if strTestNum == "mc_timer_t2_t1":

			strLastReceivedTimeStamp = kip_ApiVariables["RcvTimeStamp"]
			nLastReceivedTimeStamp = Hex2Int(strLastReceivedTimeStamp)     	# t2

			strTimeStamp_t1 = kip_ApiVariables["time_stamp_t1"] 
			nTimeStamp_t1 = Hex2Int(strTimeStamp_t1)                       	# t1 

			strCompareValue= j_parameter["compare_value"] 
			nCompareValue = int(strCompareValue)                       		# value to check 

			strCompareFunction = j_parameter["compare_function"]            # compare function

			nDiff_t2_t1 = nLastReceivedTimeStamp - nTimeStamp_t1           	# t2-t1

			if strCompareFunction == "GTorE":  # Greater Than or Equal
				if nDiff_t2_t1 >=  nCompareValue:
					strMessage = "PYTHON EVALUATION--> t2-t1 difference is in the desired range" + " t1:" + str(nTimeStamp_t1) +  "  t2:" + str(nLastReceivedTimeStamp) + " diff:" + str (nDiff_t2_t1)
					strResult = "ok"

				else:
					strMessage = "PYTHON EVALUATION--> t2-t1 difference is NOT in the desired range" + " t1:" + str(nTimeStamp_t1) +  "  t2:" + str(nLastReceivedTimeStamp) + " diff:" + str (nDiff_t2_t1)
					strResult = "af"

			elif strCompareFunction == "LT":  # Less Than
				if nDiff_t2_t1 <  nCompareValue:
					strMessage = "PYTHON EVALUATION--> t2-t1 difference is in the desired range" + " t1:" + str(nTimeStamp_t1) +  "  t2:" + str(nLastReceivedTimeStamp) + " diff:" + str (nDiff_t2_t1)
					strResult = "ok"

				else:
					strMessage = "PYTHON EVALUATION--> t2-t1 difference is NOT in the desired range" + " t1:" + str(nTimeStamp_t1) +  "  t2:" + str(nLastReceivedTimeStamp) + " diff:" + str (nDiff_t2_t1)
					strResult = "af"

			elif strCompareFunction == "LTorE":  # Less Than or Equal
				if nDiff_t2_t1 <=  nCompareValue:
					strMessage = "PYTHON EVALUATION--> t2-t1 difference is in the desired range" + " t1:" + str(nTimeStamp_t1) +  "  t2:" + str(nLastReceivedTimeStamp) + " diff:" + str (nDiff_t2_t1)
					strResult = "ok"

				else:
					strMessage = "PYTHON EVALUATION--> t2-t1 difference is NOT in the desired range" + " t1:" + str(nTimeStamp_t1) +  "  t2:" + str(nLastReceivedTimeStamp) + " diff:" + str (nDiff_t2_t1)
					strResult = "af"


		#-------------------------------------------------------------------------------------------------

		if strTestNum == "set_time_before_event":
	
			kip_ApiVariables["time_before_event"] = kip_ApiVariables["AutoNotifyTimerTime"]		
				

			strMessage = "PYTHON VALUE SETTING--> time_before_event is set "
			strResult = "ok"

		#-------------------------------------------------------------------------------------------------

		if strTestNum == "set_time_after_event":
	
			kip_ApiVariables["time_after_event"] = kip_ApiVariables["AutoNotifyTimerTime"]		
				

			strMessage = "PYTHON VALUE SETTING--> time_after_event is set "
			strResult = "ok"		
		#-------------------------------------------------------------------------------------------------

		if strTestNum == "compare_time_before_after_event":

			strTimeBeforeEvent=	kip_ApiVariables["time_before_event"]
			strTimeAfterEvent = kip_ApiVariables["time_after_event"]			

			nTimeBeforeEvent = Hex2Int(strTimeBeforeEvent)
			nTimeAfterEvent = Hex2Int(strTimeAfterEvent)
			nTimeDifms = (nTimeAfterEvent - nTimeBeforeEvent) /10000

			strCompareValue= j_parameter["compare_value"] 
			nCompareValue = int(strCompareValue)                       		# value to check 

			strCompareFunction = j_parameter["compare_function"]            # compare function

			if strCompareFunction == "GTorE":  # Greater Than or Equal
				if nTimeDifms >=  nCompareValue:
					strMessage = "PYTHON EVALUATION--> t2-t1 difference is in the desired range" + " t1:" + str(nTimeBeforeEvent) +  "  t2:" + str(nTimeAfterEvent) + " diff:" + str (nTimeDifms)
					strResult = "ok"
				else:
					strMessage = "PYTHON EVALUATION--> t2-t1 difference is NOT in the desired range" + " t1:" + str(nTimeBeforeEvent) +  "  t2:" + str(nTimeAfterEvent) + " diff:" + str (nTimeDifms)
					strResult = "af"

			elif strCompareFunction == "LTorE":  # Less Than or Equal
				if nTimeDifms <=  nCompareValue:
					strMessage = "PYTHON EVALUATION--> t2-t1 difference is in the desired range" + " t1:" + str(nTimeBeforeEvent) +  "  t2:" + str(nTimeAfterEvent) + " diff:" + str (nTimeDifms)
					strResult = "ok"
				else:
					strMessage = "PYTHON EVALUATION--> t2-t1 difference is NOT in the desired range" + " t1:" + str(nTimeBeforeEvent) +  "  t2:" + str(nTimeAfterEvent) + " diff:" + str (nTimeDifms)
					strResult = "af"	

		#-------------------------------------------------------------------------------------------------

		if strTestNum == "70106":

			strLastReceivedTimeStamp = kip_ApiVariables["RcvTimeStamp"]
			nLastReceivedTimeStamp = Hex2Int(strLastReceivedTimeStamp)     

			if nLastReceivedTimeStamp > Hex2Int("0000FFFF0000") :
				strMessage = "PYTHON EVALUATION--> Received timestamp is greater than h(00 00 FF FF 00 00)"
				strResult = "ok"

			else:
				strMessage = "PYTHON EVALUATION--> Received time-stamp is not greater than h(00 00 FF FF 00 00)"
				strResult = "af"

		#-------------------------------------------------------------------------------------------------

		if strTestNum == "70108":

			strLastReceivedTimeStamp = kip_ApiVariables["RcvTimeStamp"]
			nLastReceivedTimeStamp = Hex2Int(strLastReceivedTimeStamp)   

			# strTimeBeforeEvent=	kip_ApiVariables["time_before_event"]
			# strTimeAfterEvent = kip_ApiVariables["AutoNotifyTimerTime"]			

			# nTimeBeforeEvent = Hex2Int(strTimeBeforeEvent)
			# nTimeAfterEvent = Hex2Int(strTimeAfterEvent)
			# nTimeDifms = (nTimeAfterEvent - nTimeBeforeEvent) /10000


			if nLastReceivedTimeStamp > Hex2Int("0000FFFF0000") :
				
				strMessage = "PYTHON EVALUATION--> Received timestamp is greater than h(00 00 FF FF 00 00), Received value: " +  strLastReceivedTimeStamp
				strResult = "ok"

			# elif  nLastReceivedTimeStamp == 0:
			# 	strMessage = "PYTHON EVALUATION--> Received timestamp is 0"
			# 	strResult = "ok"

			else:
				strMessage = "PYTHON EVALUATION--> Received time-stamp is not greater than h(00 00 FF FF 00 00), Received value: " +  strLastReceivedTimeStamp
				strResult = "af"

		#-------------------------------------------------------------------------------------------------	
        
		if strTestNum == "70109":

			 # Find test time from system clock 

			strTimeBeforeEvent=	kip_ApiVariables["time_before_event"]        #s1
			strTimeAfterEvent = kip_ApiVariables["time_after_event"]         #s2

			nTimeBeforeEvent = Hex2Int(strTimeBeforeEvent)
			nTimeAfterEvent = Hex2Int(strTimeAfterEvent)
			nTimeDifms = (nTimeAfterEvent - nTimeBeforeEvent) /10000          #s2-s1 ms

			# Find timenotify values dif for DUT
			strLastReceivedTimeStamp = kip_ApiVariables["RcvTimeStamp"]
			nLastReceivedTimeStamp = Hex2Int(strLastReceivedTimeStamp)     	# t2

			strTimeStamp_t1 = kip_ApiVariables["time_stamp_t1"] 
			nTimeStamp_t1 = Hex2Int(strTimeStamp_t1)                       	# t1 

			nDiff_t2_t1 = nLastReceivedTimeStamp - nTimeStamp_t1           	# t2-t1 ms


			nAcc= abs(nTimeDifms - nDiff_t2_t1)

			if nAcc < 52:
				strMessage = "PYTHON EVALUATION--> Clock accuricy is in range"
				strResult = "ok"

			else:
				strMessage = "PYTHON EVALUATION--> Clock accuricy is NOT in range"
				strResult = "af"

		#-------------------------------------------------------------------------------------------------	
        
		if strTestNum == "70110":
			strLastReceivedTimeStamp = kip_ApiVariables["RcvTimeStamp"]
			nLastReceivedTimeStamp = Hex2Int(strLastReceivedTimeStamp) 

			if nLastReceivedTimeStamp <= 10050:
				strMessage = "PYTHON EVALUATION--> Received timestamp is less than or equal to 10050"
				strResult = "ok"

			else:
				strMessage = "PYTHON EVALUATION--> Received time-stamp is greater than 10050"
				strResult = "af"

		#-------------------------------------------------------------------------------------------------	        

		if strTestNum == "70111":

			strTimeBeforeEvent=	kip_ApiVariables["time_before_event"]
			strTimeAfterEvent = kip_ApiVariables["AutoNotifyTimerTime"]			

			nTimeBeforeEvent = Hex2Int(strTimeBeforeEvent)
			nTimeAfterEvent = Hex2Int(strTimeAfterEvent)
			nTimeDifms = (nTimeAfterEvent - nTimeBeforeEvent) /10000

			if  (( nTimeDifms> 9950)  and (nTimeDifms <= 10650) ) :
				strMessage = "{PYTHON EVALUATION--> Received periodic timer notify is in expected time frame." + "Time Dif: " + str(nTimeDifms) + " ms"
				strResult = "ok"

			else:
				strMessage = "PYTHON EVALUATION--> Received periodic timer notify is NOT in expected time frame." + "Time Dif: " + str(nTimeDifms) + "ms"
				strResult = "af"

			kip_ApiVariables["time_before_event"] = kip_ApiVariables["AutoNotifyTimerTime"]

		#-------------------------------------------------------------------------------------------------	

		if strTestNum == "70120":

			strLastReceivedTimeStamp = kip_ApiVariables["RcvTimeStamp"]
			nLastReceivedTimeStamp = Hex2Int(strLastReceivedTimeStamp)     

			if nLastReceivedTimeStamp < Hex2Int("0000FFFF0000") :
				strMessage = "PYTHON EVALUATION--> Received timestamp is lower than h(00 00 FF FF 00 00)"
				strResult = "ok"

			else:
				strMessage = "PYTHON EVALUATION--> Received time-stamp is not lower than h(00 00 FF FF 00 00)"
				strResult = "af"

		#-------------------------------------------------------------------------------------------------
		
		if strTestNum == "70123":

			strLastReceivedTimeStamp = kip_ApiVariables["RcvTimeStamp"]
			nLastReceivedTimeStamp = Hex2Int(strLastReceivedTimeStamp)  

			if (nLastReceivedTimeStamp > int("0x100000000",16)) and (nLastReceivedTimeStamp < int("0x10000000000",16) ):
				strMessage = "PYTHON EVALUATION--> Received timestamp is    " + strLastReceivedTimeStamp
				strResult = "ok"

			else:
				strMessage = "PYTHON EVALUATION--> Received time-stamp is not in range"
				strResult = "af"

		#-------------------------------------------------------------------------------------------------		

		if strTestNum == "70124":

			strLastReceivedTimeStamp = kip_ApiVariables["RcvTimeStamp"]
			nLastReceivedTimeStamp = Hex2Int(strLastReceivedTimeStamp)  

			if (nLastReceivedTimeStamp > int("0x989680",16)):
				strMessage = "PYTHON EVALUATION--> Received timestamp is    " + strLastReceivedTimeStamp
				strResult = "ok"

			else:
				strMessage = "PYTHON EVALUATION--> Received time-stamp is not in range"
				strResult = "af"

		#-------------------------------------------------------------------------------------------------

		if strTestNum == "timing_test":

			strMcShiftValue= j_parameter["mc_shift_value"]
			nMcShiftValue = int(strMcShiftValue)

			#time.sleep(9)

			nNewTimeStamp = (nAutoNotiftTimerValue)
			nNewTimeStamp = (nNewTimeStamp + nMcShiftValue)
			strNewTimeStamp= Int2Hex(nNewTimeStamp,6)

			kip_ApiVariables["AutoNotifyTimerValue"]= strNewTimeStamp
			kip_ApiVariables["new_time_stamp"] = strNewTimeStamp

			strMessage = "MC_TIMER_HEX: " + strAutoNotiftTimerValue + "   MC_TIMER_DEC: " + str(nAutoNotiftTimerValue) + "   NEW TS HEX: " + strNewTimeStamp + "   NEW TS DEC: " + str(nNewTimeStamp)  + "   shift_value:" + str(nMcShiftValue) 
			strResult = "ok"

		#-------------------------------------------------------------------------------------------------

		if strTestNum == "mc_timer_set":

			kip_ApiVariables["AutoNotifyTimerValue"]= "00 00 00 00 00 00" 

			strMessage = "MC_TIMER is set to 0"
			strResult = "ok"

		#-------------------------------------------------------------------------------------------------


		# return result data as JSON string for evaluation in EITT
		return "{ \"message\": \"'" + strMessage + "'\", \"result\" : \"" + strResult + "\"}"
  
        

	except BaseException as e:
		exc_type, exc_obj, exc_tb = sys.exc_info()
		error_msg = f'Python exception: {kip_get_fn()}, {type(e).__name__} in line {exc_tb.tb_lineno} occurred: {str(e)}'
		_script_last_error = error_msg
		logging.debug(error_msg)
		return "{ \"message\": \"'" + error_msg + "'\", \"result\" : \"af\"}"


