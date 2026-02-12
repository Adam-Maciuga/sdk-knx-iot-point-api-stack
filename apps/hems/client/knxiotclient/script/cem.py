#!/usr/bin/env -S poetry run python3
"""
Script to:
- discover a KNX IoT device
- to negotiate a new master secret
- to set IID + IA
- to reset the device
- to set the LSM to unload/loading/loaded
- to set the RCP, PUB tables
- to set the group addresses
- to set group keys
- to clean up the toolkey
- etc.
"""

from knxiotclient.coap import OSCORECredentials
from knxiotclient.coap import KNXIoTCoAPClient
from knxiotclient.coap import byte_array_compare
from knxiotclient.pase import KNXIoTPaseUtils
from knxiotclient.mdns import discover_device_by_serial
import asyncio
import re
import sys
import cbor2
from ecdsa import ellipticcurve
from Crypto.Util.number import bytes_to_long
from Crypto.Util.number import long_to_bytes

# hardcoded pw for testing
pw = "2X4W3TE0DFLLS19Y1FCH" # shall be provided by user via QR code or similar

def parse_core_link_format(payload: str) -> dict:
    """
    Parse CoRE Link Format response to extract resources.

    Args:
        payload: CoRE Link Format string (e.g., "</dev>;rt="urn:knx:fb.0",</auth>;...")

    Returns:
        Dictionary mapping resource paths to their attributes
    """
    resources = {}

    # Match <path>;attributes pattern
    pattern = r'<([^>]+)>([^<]*)'
    matches = re.findall(pattern, payload)

    for path, attributes in matches:
        attrs = {}
        # Parse attributes like rt="urn:knx:fb.0";ct=40
        attr_pattern = r'(\w+)=("[^"]+"|[^;,]+)'
        attr_matches = re.findall(attr_pattern, attributes)

        for key, value in attr_matches:
            # Remove quotes if present
            attrs[key] = value.strip('"')

        resources[path] = attrs

    return resources

def decode_cbor_data(data: bytes) -> str:
    """
    Attempt to decode CBOR data.

    Args:
        data: Raw bytes to decode

    Returns:
        Decoded string representation
    """
    try:
        decoded = cbor2.loads(data)
        return str(decoded)
    except Exception:  # pylint: disable=broad-except
        # Not CBOR, try UTF-8
        try:
            return data.decode('utf-8')
        except Exception:  # pylint: disable=broad-except
            # Return hex representation
            return f"0x{data.hex()}"

def select_address(device) -> str:
    """
    Select best IPv6 address from device.

    Args:
        device: KNXIoTDevice instance

    Returns:
        Selected IPv6 address or None
    """
    # Prefer link-local IPv6 (fe80::) for direct communication
    for addr in device.addresses:
        if addr.startswith('fe80::'):
            print(f"Using link-local IPv6 address: {addr}")
            return addr

    # Fall back to Thread ULA (fd8d::) for Thread devices
    for addr in device.addresses:
        if addr.startswith('fd8d::'):
            print(f"Using Thread ULA address: {addr}")
            return addr

    # Use first available address
    if device.addresses:
        addr = device.addresses[0]
        print(f"Using address: {addr}")
        return addr

    print("No valid address found!")
    return ""

async def discover_device(serial_number: str, timeout: float = 3.0):
    """
    Discover device by serial number.

    Args:
        serial_number: Device serial number
        timeout: Discovery timeout in seconds

    Returns:
        Discovered device or None
    """
    print(f"\n{'='*70}")
    print("Device Discovery")
    print()
    print(f"Looking for device with serial number: {serial_number}")
    print(f"Timeout: {timeout} seconds\n")

    device = await asyncio.to_thread(discover_device_by_serial, serial_number, timeout)

    if not device:
        print(f"Device {serial_number} not found!")
        return None

    print(f"Device found: {device.name}")
    print(f"  Addresses:\n  - " + "\n  - ".join(device.addresses))
    print(f"  Port: {device.port}")

    if device.sleep_period:
        print(f"  Sleep Period: {device.sleep_period}ms")
    if device.role:
        print(f"  Role: {device.role}")

    return device

async def request_and_verify_pase_parameters(client: KNXIoTCoAPClient, host: str, port: int):
    
    id = "-------"
    rnd = bytes.fromhex("0102030405060708091011121314151617181920212223242526272829303132")  
    request_payload = cbor2.dumps({0: id, 15: rnd})

    resource_name = "/.well-known/knx/spake"
   
    # CBOR content format   
    ret = await client.write2(host, port, resource_name, request_payload, content_format=60)     
    
    return ret

async def request_and_verify_credential_request(client: KNXIoTCoAPClient, host: str, port: int, X: ellipticcurve.Point):
    
    request_payload = cbor2.dumps({10:  KNXIoTPaseUtils.encode_point_uncompressed(X)})

    resource_name = "/.well-known/knx/spake"
   
    # CBOR content format   
    ret = await client.write2(host, port, resource_name, request_payload, content_format=60)        
   
    return ret

async def request_and_verify_verification_request(client: KNXIoTCoAPClient, host: str, port: int, confP: bytes):
    
    request_payload = cbor2.dumps({14: confP})

    resource_name = "/.well-known/knx/spake"
   
    # CBOR content format   
    ret = await client.write2(host, port, resource_name, request_payload, content_format=60)     
   
    return ret

async def write_resource(client: KNXIoTCoAPClient, host: str, port: int, credentials, path: str, payload: bytes):
    
    # CBOR content format
    ret = await client.write2(host, port, path, payload, credentials, None, content_format=60)
    
    return ret

async def delete_auth_at(client: KNXIoTCoAPClient, host: str, port: int, credentials):

    auth_at_path = "/auth/at/-------"

      # No content format
    ret = await client.delete(host, port, auth_at_path, b"", credentials, None, None)

    return ret

async def main():
    """Main script function."""
    if len(sys.argv) < 1:
        print("Usage: python CEM <serial_number>")
        print("\nExample:")
        print("  python CEM 00fd5f00002e")
        sys.exit(1)

    serial_number = sys.argv[1]

    print("=" * 70)
    print("KNX IoT: python based commissioning tool for CEM devices")
    print("=" * 70)
    print(f"Serial Number: {serial_number}")

    # discover device
    device = await discover_device(serial_number)
    if not device:
        sys.exit(1)

    # select best address
    host = select_address(device)
    if not host:
        sys.exit(1)

    port = device.port

    # set initial OSCORE credentials with empty master_secret
    credentials = OSCORECredentials(
        key_identifier="",
        master_secret="",
        context_id="",
        serial_number=serial_number,
        algorithm="AES-CCM-16-64-128"
    )

    # no master_secret -> no OSCORE applied
    client = KNXIoTCoAPClient()

    # pase parameter request and verify     
    ret = await request_and_verify_pase_parameters(client, host, port)

    if ret.success:
        if ret.payload:
            decoded = cbor2.loads(ret.payload)
    else:
        print("No payload received from device!")
        sys.exit(1)

    print("=" * 70)
    print("request and verify pase parameters")    
    
    rcv_iter = decoded[12][16]
    rcv_salt = decoded[12][5]

    assert isinstance(rcv_salt, bytes)
    
    computed_pw = KNXIoTPaseUtils.compute_pw(pw)

    w0s_and_w1s =  KNXIoTPaseUtils.derive_key(computed_pw, rcv_salt, rcv_iter, KNXIoTPaseUtils.key_length)

    w0s = w0s_and_w1s[:KNXIoTPaseUtils.key_length // 2]
    w1s = w0s_and_w1s[KNXIoTPaseUtils.key_length // 2:]

    M = KNXIoTPaseUtils.decode_point(KNXIoTPaseUtils.s_crv_M)
    N = KNXIoTPaseUtils.decode_point(KNXIoTPaseUtils.s_crv_N)
   
    P = KNXIoTPaseUtils.generator

    w0 = bytes_to_long(w0s) % KNXIoTPaseUtils.order
    w1 = bytes_to_long(w1s) % KNXIoTPaseUtils.order

    # fixed x private key for testing, shall be random in real implementation
    x_private_key = bytes.fromhex("3566463fad55b9306ab9a51427a23394c022c5860445291a3c3a0628b4cea7ad")

    x = bytes_to_long(x_private_key)

    w0M = KNXIoTPaseUtils.to_affine(w0 * M)
    w0N = KNXIoTPaseUtils.to_affine(w0 * N)
    xP  = KNXIoTPaseUtils.to_affine(x * P)

    X = xP + w0M

    # credential request and verify
    ret = await request_and_verify_credential_request(client, host, port, X)

    if ret.success:
        if ret.payload:
            decoded = cbor2.loads(ret.payload)
    else:
        print("No payload received from device!")
        sys.exit(1)
    
    print("=" * 70)
    print("request and verify credential request")

    rcv_shareV = decoded[11]
    rcv_confmV = decoded[13]

    Y = KNXIoTPaseUtils.decode_point(rcv_shareV.hex())

    Y_minus_w0N = Y + KNXIoTPaseUtils.point_neg(w0N, KNXIoTPaseUtils.p)
    X_minus_w0M = X + KNXIoTPaseUtils.point_neg(w0M, KNXIoTPaseUtils.p)

    Z_prover = Y_minus_w0N * x * KNXIoTPaseUtils.h
    V_prover = Y_minus_w0N * w1 * KNXIoTPaseUtils.h

    TT = KNXIoTPaseUtils.compute_tt(KNXIoTPaseUtils.encode_point_uncompressed(X), KNXIoTPaseUtils.encode_point_uncompressed(Y), KNXIoTPaseUtils.encode_point_uncompressed(Z_prover), KNXIoTPaseUtils.encode_point_uncompressed(V_prover), long_to_bytes(w0))

    K_main = KNXIoTPaseUtils.compute_main_secret(TT)

    K_shared = KNXIoTPaseUtils.compute_shared_secret(K_main)

    K_confirmation = KNXIoTPaseUtils.compute_confirmation_secret(K_main)

    half = len(K_confirmation) // 2
    kConfirmP = K_confirmation[:half]
    kConfirmV = K_confirmation[half:]

    confirmP = KNXIoTPaseUtils.mac(kConfirmP, KNXIoTPaseUtils.encode_point_uncompressed(Y))
    confirmV = KNXIoTPaseUtils.mac(kConfirmV, KNXIoTPaseUtils.encode_point_uncompressed(X))

    print("=" * 70)   
    if(byte_array_compare(confirmV, rcv_confmV)):
        print("confirmV = ok")
    else:
        print("confirmV = NOT ok")
        sys.exit(1)

    print("ms:", K_shared[:16].hex()) # could be useful for debugging (wireshark etc)

    # verification request and verify, this sets the shared master secret as toolkey
    ret = await request_and_verify_verification_request(client, host, port, confirmP)   

    if ret.success:
        if ret.payload:
            decoded = cbor2.loads(ret.payload)
    else:
        print("No payload received from device!")
        sys.exit(1)

    print("=" * 70)
    print("request and verify verification request")

    # clean up CoAP client
    try:
        await client.close()
        await asyncio.sleep(0.1)
    except Exception:  
        pass   
  
    credentials.key_identifier = "2d2d2d2d2d2d2d"
    credentials.master_secret = K_shared[:16].hex()

    # master_secret set -> OSCORE applied   
    client = KNXIoTCoAPClient()  

    # toolkey exchange, this sets the new toolkey
    path = "/auth/at"
    payload = cbor2.dumps([{0: "0c00fa10010c00", 8: {4: {0: bytes.fromhex('0c00fa10010c00'), 2: bytes.fromhex('ff112233445566778899aabbccddeeff')}}, 9: ["if.c", "if.p", "if.d", "if.sec", "if.swu"], 38: 2}])
    ret = await write_resource(client, host, port, credentials, path, payload)

    if ret.success:
        if ret.payload:
            decoded = cbor2.loads(ret.payload)
    else:
        print("No payload received from device!")
        sys.exit(1)

    print("=" * 70)
    print("set temp toolkey via /auth/at")

    # clean up CoAP client
    try:
        await client.close()
        await asyncio.sleep(0.1)
    except Exception:  
        pass

    credentials.key_identifier = "0c00fa10010c00"
    credentials.master_secret = 'ff112233445566778899aabbccddeeff'
    credentials.context_id = 'aa'

    # master_secret set -> OSCORE applied
    client = KNXIoTCoAPClient()

    # toolkey exchange, this deletes the shared master secret
    ret = await delete_auth_at(client, host, port, credentials)
 
    if ret.success:
        if ret.payload:
            decoded = cbor2.loads(ret.payload)
    else:
        print("No payload received from device!")
        sys.exit(1)

    print("delete temp toolkey via /auth/at")

    path = "/.well-known/knx/ia"
    payload = cbor2.dumps({12: 4354, 26: 4328719365}) # IID + IA: 0102030405 + 1.1.2
    ret = await write_resource(client, host, port, credentials, path, payload)
    if not ret.success:
        print("Setting IID + IA failed!")
        sys.exit(1) 

    print(f"set {path} ")

    path = "/.well-known/knx"
    payload = cbor2.dumps({1: 7, 2: "reset"})
    ret = await write_resource(client, host, port, credentials, path, payload)
    if not ret.success:
        print("Reset failed!")
        sys.exit(1) 

    print(f"set {path} ")

    # wait for 2 seconds after reset
    await asyncio.sleep(2)

    path = "/a/lsm"
    payload = cbor2.dumps({2: 4}) # unload
    ret = await write_resource(client, host, port, credentials, path, payload)
    if not ret.success:
        print("Unload failed!")
        sys.exit(1) 

    path = "/a/lsm"
    payload = cbor2.dumps({2: 1}) # loading
    ret = await write_resource(client, host, port, credentials, path, payload)
    if not ret.success:
        print("Start loading failed!")
        sys.exit(1) 

    path = "/fp/r"
    payload = cbor2.dumps([{0: 0, 7: [1, 2], 13: 16384000}])
    ret = await write_resource(client, host, port, credentials, path, payload)
    if not ret.success:
        print("rcp failed!")
        sys.exit(1) 

    print(f"set {path} ")

    path = "/fp/p"
    payload = cbor2.dumps([{0: 0, 7: [1, 2], 13: 16384000}])
    ret = await write_resource(client, host, port, credentials, path, payload)
    if not ret.success:
        print("pub failed!")
        sys.exit(1) 

    print(f"set {path} ")

    path = "/fp/g"
    payload = cbor2.dumps([{0: 0, 7: [1], 8: 16, 11: "/p/inverter"}])
    ret = await write_resource(client, host, port, credentials, path, payload)
    if not ret.success:
        print("group failed!")
        sys.exit(1) 

    print(f"set {path} ")

    path = "/fp/g"
    payload = cbor2.dumps([{0: 1, 7: [2], 8: 64, 11: "/p/charger"}])
    ret = await write_resource(client, host, port, credentials, path, payload)
    if not ret.success:
        print("group failed!")
        sys.exit(1) 

    print(f"set {path} ")

    path = "/auth/at"
    payload = cbor2.dumps([{0: "1", 8: {4: {0: bytes.fromhex('0001'), 2: bytes.fromhex('ff0102030405060708090A0B0C0D0E0F'), 6: bytes.fromhex('10020C000001')}}, 9: [1], 38: 2}])
    ret = await write_resource(client, host, port, credentials, path, payload)
    if not ret.success:
        print("group key failed!")
        sys.exit(1) 

    print(f"set {path} ")

    path = "/auth/at"
    payload = cbor2.dumps([{0: "2", 8: {4: {0: bytes.fromhex('0002'), 2: bytes.fromhex('ff0102030405060708090A0B0C0D0E0F'), 6: bytes.fromhex('10020C000002')}}, 9: [2], 38: 2}])
    ret = await write_resource(client, host, port, credentials, path, payload)
    if not ret.success:
        print("group key failed!")
        sys.exit(1) 

    print(f"set {path} ")

    path = "/a/lsm"
    payload = cbor2.dumps({2: 2}) # loaded
    ret = await write_resource(client, host, port, credentials, path, payload)
    if not ret.success:
        print("Loaded failed!")
        sys.exit(1) 

    print("=" * 70)
    print("commissioning completed")
    print("=" * 70)

    # clean up CoAP client
    try:
        await client.close()
        await asyncio.sleep(0.1)
    except Exception:  
        pass

if __name__ == "__main__":
    asyncio.run(main())