"""
KNX IoT CoAP Client with OSCORE Security.

Implements read, write, and observe operations for KNX IoT Point API
using CoAP with OSCORE security as specified in 3_10_5 KNX IoT Point API.pdf
"""
import asyncio
import binascii
import hashlib
import json
import logging
import cbor2
from pathlib import Path
from typing import Optional, Callable
from urllib.parse import urlparse
from aiocoap import Context, Message, Code  
from aiocoap.numbers.codes import Code as CoAPCode  
from aiocoap.credentials import CredentialsMap  
from aiocoap import oscore  
from aiocoap import tokenmanager  
from aiocoap.message import Message
from dataclasses import dataclass

logger = logging.getLogger(__name__)

@dataclass
class OSCORECredentials:
    """OSCORE security credentials for a device or group address."""
    key_identifier: str  # Sender/Recipient ID
    master_secret: str  # Shared secret key (hex string)
    context_id: str = ""  # ID Context (hex string)
    master_salt: str = ""  # Master Salt (hex string)
    individual_address: str = ""  # Individual address (e.g., "2.0.0")
    serial_number: str = ""  # Device serial number
    algorithm: str = "AES-CCM-16-64-128"  # OSCORE algorithm

@dataclass
class WriteResult:
    success: bool
    payload: Optional[bytes] = None
    response_code: Optional[object] = None  # can be CoAPCode

def byte_array_compare(a1: bytes, a2: bytes) -> bool:
    if len(a1) != len(a2):
        return False
    for x, y in zip(a1, a2):
        if x != y:
            return False
    return True

# Force 8-byte CoAP tokens; aiocoap otherwise strips leading zeroes.
def _next_token_8(self):
    self._token = (self._token + 1) % (2**64)
    return self._token.to_bytes(8, "big")

tokenmanager.TokenManager.next_token = _next_token_8

# Force 4-byte OSCORE Partial IVs
def _build_new_nonce_4(self, alg):
    seqno = self.new_sequence_number()
    if seqno >= (1 << 32):
        raise oscore.ContextUnavailable(
            "Sequence number too large for 4-byte Partial IV."
        )
    partial_iv = seqno.to_bytes(4, "big")
    return (
        self._construct_nonce(partial_iv, self.sender_id, alg),
        partial_iv,
    )

setattr(oscore.BaseSecurityContext, '_build_new_nonce', _build_new_nonce_4)
setattr(oscore.CanProtect, '_build_new_nonce', _build_new_nonce_4)

if hasattr(oscore, 'FilesystemSecurityContext'):
    setattr(oscore.FilesystemSecurityContext, '_build_new_nonce', _build_new_nonce_4)

# Monkey-patch FilesystemSecurityContext to suppress Windows cleanup errors
if hasattr(oscore, 'FilesystemSecurityContext'):
    _original_fsc_del = oscore.FilesystemSecurityContext.__del__

    def _safe_fsc_del(self):
        """Windows-safe cleanup for FilesystemSecurityContext."""
        try:
            _original_fsc_del(self)
        except (PermissionError, AttributeError, OSError):
            # Suppress Windows file lock errors during cleanup
            pass

    oscore.FilesystemSecurityContext.__del__ = _safe_fsc_del

class KNXIoTCoAPClient:
    """CoAP client for KNX IoT devices with OSCORE security."""

    def __init__(self):
        """Initialize the CoAP client."""
        self.context: Optional[Context] = None
        self._oscore_contexts = {}  # Cache OSCORE security contexts by serial number
        self._credentials_map = None  # Store credentials for context creation

    def _decode_payload_by_content_type(self, payload: bytes, content_format: Optional[int]) -> dict:
        """
        Decode payload based on CoAP content format.

        Args:
            payload: Raw payload bytes
            content_format: CoAP content format code
                (e.g., 40=CBOR, 50=JSON, 40=application/link-format)

        Returns:
            Dictionary with 'decoded' data and 'format' type
        """
        if not payload:
            return {'decoded': None, 'format': 'empty'}

        # CoAP Content-Format codes:
        # 0 = text/plain
        # 40 = application/link-format
        # 50 = application/json
        # 60 = application/cbor
        # 41 = application/xml
        # 42 = application/octet-stream

        # Try decoding based on content format
        if content_format == 40:  # application/link-format (CoRE Link Format)
            try:
                text = payload.decode('utf-8')
                return {'decoded': text, 'format': 'link-format'}
            except UnicodeDecodeError:
                return {'decoded': payload, 'format': 'binary'}

        elif content_format == 50:  # application/json
            try:
                text = payload.decode('utf-8')
                data = json.loads(text)
                return {'decoded': data, 'format': 'json'}
            except (UnicodeDecodeError, json.JSONDecodeError) as e:
                logger.debug("Failed to decode as JSON: %s", e)
                return {'decoded': payload, 'format': 'binary'}

        elif content_format == 60:  # application/cbor
            try:
                decoded = cbor2.loads(payload)
                return {'decoded': decoded, 'format': 'cbor'}
            except Exception as e:  # pylint: disable=broad-except
                logger.debug("Failed to decode as CBOR: %s", e)
                return {'decoded': payload, 'format': 'binary'}

        # No content format or unknown - try to auto-detect
        # Try CBOR first (most common for KNX IoT)
        try:
            decoded = cbor2.loads(payload)
            return {'decoded': decoded, 'format': 'cbor'}
        except Exception:  # pylint: disable=broad-except
            pass

        # Try JSON
        try:
            text = payload.decode('utf-8')
            data = json.loads(text)
            return {'decoded': data, 'format': 'json'}
        except (UnicodeDecodeError, json.JSONDecodeError):
            pass

        # Try UTF-8 text
        try:
            text = payload.decode('utf-8')
            return {'decoded': text, 'format': 'text'}
        except UnicodeDecodeError:
            pass

        # Binary data
        return {'decoded': payload, 'format': 'binary'}

    def _log_response_payload(self, response, resource_path: str, method: str = "GET"):
        """
        Log response payload in human-readable format.

        Uses content format from response to decode appropriately.

        Args:
            response: CoAP response message
            resource_path: Resource path for context
            method: HTTP method (GET, PUT, etc.)
        """
        if not response.payload:
            logger.info("Response payload for %s: <empty>", resource_path)
            return

        # Log request information first
        logger.info("Request: %s %s", method, resource_path)

        payload_hex = response.payload.hex()
        logger.info("Response payload (hex): %s", payload_hex)
        logger.info("Response payload (size): %s bytes", len(response.payload))

        # Get content format from response
        content_format = getattr(response.opt, 'content_format', None)
        if content_format is not None:
            logger.info("Response content format: %s", content_format)

        # Decode based on content type
        decoded_info = self._decode_payload_by_content_type(response.payload, content_format)

        if decoded_info['format'] == 'cbor':
            json_payload = json.dumps(decoded_info['decoded'], indent=2, default=str)
            logger.info("Response payload (CBOR decoded):\n%s", json_payload)
        elif decoded_info['format'] == 'json':
            json_payload = json.dumps(decoded_info['decoded'], indent=2, default=str)
            logger.info("Response payload (JSON):\n%s", json_payload)
        elif decoded_info['format'] in ['text', 'link-format']:
            logger.info(
                "Response payload (%s): %s", decoded_info['format'], decoded_info['decoded']
            )
        else:
            # Binary data - just show hex (already logged above)
            logger.info("Response payload: <binary data, see hex above>")

    async def _ensure_context(self):
        """Ensure CoAP context is initialized with OSCORE support."""
        if self.context is None:
            # Create credentials map BEFORE creating context
            if self._credentials_map is None:
                self._credentials_map = CredentialsMap()

            # Create context with OSCORE transport enabled and credentials
            self.context = await Context.create_client_context()
            # Set credentials IMMEDIATELY after creation
            self.context.client_credentials = self._credentials_map

    async def _apply_oscore(self, uri: str, credentials: OSCORECredentials, kid_context: Optional[str] = None):
        """
        Apply OSCORE security context for a device.

        According to KNX IoT Point API spec, OSCORE uses:
        - Sender ID: Device serial number (6 bytes)
        - Recipient ID: Empty for client->server
        - Master Secret: Tool Key from Project Security.csv (16 bytes)
        - Algorithm: AES-CCM-16-64-128
        - KDF: HKDF SHA-256

        Args:
            uri: CoAP URI for the request
            credentials: OSCORE credentials
            kid_context: OSCORE Key ID Context (Installation ID) as hex string
        """
        serial_number = credentials.serial_number

        # Reuse existing context if already created for this device
        if serial_number in self._oscore_contexts:
            return

        try:
            # Convert hex credentials to bytes
            master_secret = binascii.unhexlify(credentials.master_secret)
            # Use Key Identifier as sender ID (includes 0C prefix)
            sender_id = binascii.unhexlify(credentials.key_identifier)

            # Optional parameters
            master_salt = (
                binascii.unhexlify(credentials.master_salt)
                if credentials.master_salt else b''
            )
            # Use kid_context parameter if provided, otherwise fall back
            id_context_hex = kid_context if kid_context else credentials.context_id

            # Validate hex string has even length
            if id_context_hex and len(id_context_hex) % 2 != 0:
                raise ValueError(
                    f"kid_context must have even number of hex digits, "
                    f"got: '{id_context_hex}' (length {len(id_context_hex)}). "
                    f"Example: use '01' instead of '1'"
                )

            id_context = binascii.unhexlify(id_context_hex) if id_context_hex else b''

            # Create persistent directory for OSCORE context to maintain sequence numbers
            # This ensures Partial IV (Sender Sequence Number) increments correctly
            # RFC 8613 Section 3.2: Sender Sequence Number must never repeat
            oscore_base_dir = Path('data') / 'oscore_contexts'
            oscore_base_dir.mkdir(parents=True, exist_ok=True)

            # Use serial number + id_context hash to create unique directory
            context_hash = hashlib.sha256(
                f"{serial_number}_{id_context.hex() if id_context else ''}".encode()
            ).hexdigest()[:16]
            oscore_path = oscore_base_dir / f"{serial_number}_{context_hash}"
            oscore_path.mkdir(exist_ok=True)

            # Create settings.json with OSCORE parameters
            # Based on KNX IoT Point API specification
            settings = {
                "sender-id_hex": sender_id.hex(),
                "recipient-id_hex": "",  # Empty for client requests
                "secret_hex": master_secret.hex(),
                "algorithm": "AES-CCM-16-64-128",
                "kdf": "HKDF SHA-256"
            }

            if master_salt:
                settings["salt_hex"] = master_salt.hex()

            if id_context:
                settings["id-context_hex"] = id_context.hex()

            settings_file = oscore_path / "settings.json"
            settings_file.write_text(json.dumps(settings, indent=2))

            # Create FilesystemSecurityContext from the settings file
            # The __del__ method is monkey-patched to suppress Windows cleanup errors
            security_context = oscore.FilesystemSecurityContext(str(oscore_path))

            # Ensure we have a credentials map
            if self._credentials_map is None:
                self._credentials_map = CredentialsMap()

            # If context already exists, update its credentials
            if self.context is not None and self.context.client_credentials is None:
                self.context.client_credentials = self._credentials_map

            # Apply security context to BOTH the full URI and base URI
            # aiocoap might match differently depending on version
            parsed = urlparse(uri)
            base_uri = f"{parsed.scheme}://{parsed.netloc}"

            # Add to both full and base URI for maximum compatibility
            self._credentials_map[uri] = security_context
            self._credentials_map[base_uri] = security_context
            # Also try with wildcard path
            self._credentials_map[f"{base_uri}/*"] = security_context

            # Cache the context
            self._oscore_contexts[serial_number] = security_context

        except Exception as e:
            logger.error("Failed to apply OSCORE: %s", e, exc_info=True)
            raise

    async def read(
        self,
        host: str,
        port: int,
        resource_path: str,
        credentials: Optional[OSCORECredentials] = None,
        kid_context: Optional[str] = None
    ) -> Optional[tuple]:
        """
        Read a resource from a KNX IoT device.

        Args:
            host: Device IP address
            port: CoAP port (usually 5683)
            resource_path: Resource path (e.g., "/p/o_1_1")
            credentials: OSCORE credentials for secure communication
            kid_context: OSCORE Key ID Context (Installation ID) as hex string

        Returns:
            Tuple of (payload_bytes, decoded_info_dict) or None on error
            decoded_info_dict contains {'decoded': data, 'format': format_name}
        """
        try:
            # Build CoAP URI first (needed for OSCORE setup)
            if ':' in host and not host.startswith('['):
                # IPv6 address - needs brackets
                uri = f"coap://[{host}]:{port}{resource_path}"
            else:
                # IPv4 address or already has brackets
                uri = f"coap://{host}:{port}{resource_path}"

            # Apply OSCORE credentials BEFORE creating context
            # This ensures credentials are available when context is created
            if credentials:
                await self._apply_oscore(uri, credentials, kid_context)

            # Ensure context exists before sending any request
            await self._ensure_context()
            assert self.context is not None

            # Create GET request
            request = Message(code=Code.GET, uri=uri)

            # Manually apply OSCORE protection if credentials are set
            # aiocoap's automatic OSCORE isn't working, so we do it manually
            request_id = None  # Store request_id for response decryption
            if credentials:
                serial_number = credentials.serial_number
                if serial_number in self._oscore_contexts:
                    security_context = self._oscore_contexts[serial_number]
                    try:
                        # protect() returns (protected_message, request_id)
                        # The protected message has the OSCORE payload but loses the URI/remote
                        protected_request, request_id = security_context.protect(request)
                        # Preserve the remote endpoint from the original request
                        protected_request.remote = request.remote
                        request = protected_request
                    except Exception as e:  # pylint: disable=broad-except
                        logger.error("Failed to protect request: %s", e, exc_info=True)
                else:
                    logger.warning("OSCORE context for %s not found in cache", serial_number)

            # Send request
            response = await self.context.request(request).response

            # Check for Echo in outer (encrypted) response
            if hasattr(response.opt, 'echo') and response.opt.echo:
                logger.debug("Echo option in response: %s", response.opt.echo.hex())

            # Manual OSCORE response decryption
            # If response has OSCORE option, it needs to be decrypted
            if (credentials and hasattr(response.opt, 'oscore')
                    and response.opt.oscore is not None):
                serial_number = credentials.serial_number
                if serial_number in self._oscore_contexts:
                    security_context = self._oscore_contexts[serial_number]
                    try:
                        # unprotect() requires the request_id for response messages
                        # It returns (unprotected_message, request_identifiers)
                        unprotected_response, _ = security_context.unprotect(
                            response, request_id=request_id
                        )
                        response = unprotected_response
                    except Exception as e:  # pylint: disable=broad-except
                        logger.error("Failed to unprotect response: %s", e, exc_info=True)

            # Check for Echo challenge (RFC 9175)
            # If server responds with 4.01 Unauthorized and Echo option, retry
            if (response.code == CoAPCode.UNAUTHORIZED and hasattr(response.opt, 'echo')
                    and response.opt.echo):
                echo_value = response.opt.echo
                logger.info(
                    "Echo challenge received, retrying with Echo (%s bytes)", len(echo_value)
                )

                # Create new request with Echo option
                retry_request = Message(code=Code.GET, uri=uri)

                # Add Echo option
                retry_request.opt.echo = echo_value

                # Re-protect with OSCORE if credentials provided
                retry_request_id = None
                if credentials:
                    serial_number = credentials.serial_number
                    if serial_number in self._oscore_contexts:
                        security_context = self._oscore_contexts[serial_number]
                        try:
                            protected_retry, retry_request_id = (
                                security_context.protect(retry_request)
                            )
                            protected_retry.remote = retry_request.remote
                            retry_request = protected_retry
                        except Exception as e:  # pylint: disable=broad-except
                            logger.error(
                                "Failed to protect retry request: %s", e, exc_info=True
                            )
                            return None

                # Send retry request
                retry_response = await self.context.request(retry_request).response

                # Decrypt retry response if OSCORE
                if (credentials and hasattr(retry_response.opt, 'oscore')
                        and retry_response.opt.oscore is not None):
                    if serial_number in self._oscore_contexts:
                        security_context = self._oscore_contexts[serial_number]
                        try:
                            unprotected_retry, _ = security_context.unprotect(
                                retry_response, request_id=retry_request_id
                            )
                            retry_response = unprotected_retry
                        except Exception as e:  # pylint: disable=broad-except
                            logger.error("Failed to unprotect retry response: %s", e, exc_info=True)

                # Use retry response instead of original
                response = retry_response

            # Log response payload in human-readable format
            self._log_response_payload(response, resource_path)

            if response.code.is_successful():
                # Get content format and decode
                content_format = getattr(response.opt, 'content_format', None)
                decoded_info = self._decode_payload_by_content_type(
                    response.payload, content_format
                )
                return (response.payload, decoded_info)

            logger.warning("Read failed with code: %s", response.code)
            return None

        except (OSError, RuntimeError) as e:
            logger.error("Error reading resource: %s", e, exc_info=True)
            return None

    # PUT
    async def write(
        self,
        host: str,
        port: int,
        resource_path: str,
        payload: bytes,
        credentials: Optional[OSCORECredentials] = None,
        kid_context: Optional[str] = None,
        content_format: Optional[int] = None
    ) -> bool:
        """
        Write to a resource on a KNX IoT device.

        Args:
            host: Device IP address
            port: CoAP port (usually 5683)
            resource_path: Resource path (e.g., "/p/o_1_1")
            payload: Data to write
            credentials: OSCORE credentials for secure communication
            kid_context: OSCORE Key ID Context (Installation ID) as hex string
            content_format: CoAP content format code (e.g., 60=CBOR, 50=JSON)

        Returns:
            True if successful, False otherwise
        """
        if credentials:
            await self._ensure_context()

        try:
            # Build CoAP URI - wrap IPv6 addresses in brackets
            if ':' in host and not host.startswith('['):
                # IPv6 address - needs brackets
                uri = f"coap://[{host}]:{port}{resource_path}"
            else:
                # IPv4 address or already has brackets
                uri = f"coap://{host}:{port}{resource_path}"

            # Create PUT request
            request = Message(code=Code.PUT, uri=uri, payload=payload)

            # Set content format if specified
            if content_format is not None:
                request.opt.content_format = content_format

            # Log request details
            logger.info("Request: PUT %s", resource_path)
            logger.info("Request payload (hex): %s", payload.hex())
            logger.info("Request payload (size): %s bytes", len(payload))
            if content_format is not None:
                logger.info("Request content format: %s", content_format)

                # Decode and log request payload
                decoded_info = self._decode_payload_by_content_type(payload, content_format)
                if decoded_info['format'] == 'cbor':
                    json_payload = json.dumps(decoded_info['decoded'], indent=2, default=str)
                    logger.info("Request payload (CBOR decoded):\n%s", json_payload)
                elif decoded_info['format'] == 'json':
                    json_payload = json.dumps(decoded_info['decoded'], indent=2, default=str)
                    logger.info("Request payload (JSON):\n%s", json_payload)
                elif decoded_info['format'] in ['text', 'link-format']:
                    logger.info(
                        "Request payload (%s): %s",
                        decoded_info['format'], decoded_info['decoded']
                    )

            # Apply OSCORE security if credentials provided
            if credentials:
                await self._apply_oscore(uri, credentials, kid_context)

            # Ensure context exists before sending any request
            await self._ensure_context()
            assert self.context is not None

            # Send request
            response = await self.context.request(request).response

            # Log response
            logger.info("Response code: %s", response.code)
            if response.payload:
                logger.info("Response payload (hex): %s", response.payload.hex())
                logger.info("Response payload (size): %s bytes", len(response.payload))

            if response.code.is_successful() or response.code == CoAPCode.CHANGED:
                return True

            logger.warning("Write failed with code: %s", response.code)
            return False

        except (OSError, RuntimeError) as e:
            logger.error("Error writing resource: %s", e, exc_info=True)
            return False

    # POST
    async def write2(
        self,
        host: str,
        port: int,
        resource_path: str,
        payload: bytes,
        credentials: Optional[OSCORECredentials] = None,
        kid_context: Optional[str] = None,
        content_format: Optional[int] = None
    ) -> WriteResult:
        """
        Write to a resource on a KNX IoT device.

        Args:
            host: Device IP address
            port: CoAP port (usually 5683)
            resource_path: Resource path (e.g., "/p/o_1_1")
            payload: Data to write
            credentials: OSCORE credentials for secure communication
            kid_context: OSCORE Key ID Context (Installation ID) as hex string
            content_format: CoAP content format code (e.g., 60=CBOR, 50=JSON)

        Returns:
            True if successful, False otherwise
        """
        if credentials and credentials.master_secret != "":
            await self._ensure_context()
        else:
            self.context = await Context.create_client_context()
       
        try:
            # Build CoAP URI - wrap IPv6 addresses in brackets
            if ':' in host and not host.startswith('['):
                # IPv6 address - needs brackets
                uri = f"coap://[{host}]:{port}{resource_path}"
            else:
                # IPv4 address or already has brackets
                uri = f"coap://{host}:{port}{resource_path}"

            # Create POST request
            request = Message(code=Code.POST, uri=uri, payload=payload) 
    
            # Set content format if specified
            if content_format is not None:
                request.opt.content_format = content_format

            # Log request details
            logger.info("Request: POST %s", resource_path)
            logger.info("Request payload (hex): %s", payload.hex())
            logger.info("Request payload (size): %s bytes", len(payload))
            if content_format is not None:
                logger.info("Request content format: %s", content_format)

                # Decode and log request payload
                decoded_info = self._decode_payload_by_content_type(payload, content_format)
                if decoded_info['format'] == 'cbor':
                    json_payload = json.dumps(decoded_info['decoded'], indent=2, default=str)
                    logger.info("Request payload (CBOR decoded):\n%s", json_payload)
                elif decoded_info['format'] == 'json':
                    json_payload = json.dumps(decoded_info['decoded'], indent=2, default=str)
                    logger.info("Request payload (JSON):\n%s", json_payload)
                elif decoded_info['format'] in ['text', 'link-format']:
                    logger.info(
                        "Request payload (%s): %s",
                        decoded_info['format'], decoded_info['decoded']
                    )

            # Apply OSCORE security if credentials provided                
            if credentials and credentials.master_secret != "":
                await self._apply_oscore(uri, credentials, kid_context)

            # Ensure context exists before sending any request
            await self._ensure_context()
            assert self.context is not None

            # Send request
            response = await self.context.request(request).response

            # Log response
            logger.info("Response code: %s", response.code)
            
            if response.payload:
                logger.info("Response payload (hex): %s", response.payload.hex())
                logger.info("Response payload (size): %s bytes", len(response.payload))

            success = response.code.is_successful() or response.code == CoAPCode.CHANGED

            return WriteResult(
                success=success,
                payload=response.payload if response.payload else None,
                response_code=response.code,
            )

        except (OSError, RuntimeError) as e:
            logger.error("Error writing resource: %s", e, exc_info=True)
            return WriteResult(success=False)

    async def delete(
        self,
        host: str,
        port: int,
        resource_path: str,
        payload: bytes = b'',
        credentials: Optional[OSCORECredentials] = None,
        kid_context: Optional[str] = None,
        content_format: Optional[int] = None
    ) -> WriteResult:
        """
        Write to a resource on a KNX IoT device.

        Args:
            host: Device IP address
            port: CoAP port (usually 5683)
            resource_path: Resource path (e.g., "/p/o_1_1")
            payload: Data to write
            credentials: OSCORE credentials for secure communication
            kid_context: OSCORE Key ID Context (Installation ID) as hex string
            content_format: CoAP content format code (e.g., 60=CBOR, 50=JSON)

        Returns:
            True if successful, False otherwise
        """
        if credentials and credentials.master_secret != "":
            await self._ensure_context()
        else:
            self.context = await Context.create_client_context()
       
        try:
            # Build CoAP URI - wrap IPv6 addresses in brackets
            if ':' in host and not host.startswith('['):
                # IPv6 address - needs brackets
                uri = f"coap://[{host}]:{port}{resource_path}"
            else:
                # IPv4 address or already has brackets
                uri = f"coap://{host}:{port}{resource_path}"

            # Create DELETE request
            request = Message(code=Code.DELETE, uri=uri, payload=payload)
          
            # Set content format if specified
            if content_format is not None:
                request.opt.content_format = content_format

            # Log request details
            logger.info("Request: DELETE %s", resource_path)
            logger.info("Request payload (hex): %s", payload.hex())
            logger.info("Request payload (size): %s bytes", len(payload))
            if content_format is not None:
                logger.info("Request content format: %s", content_format)

                # Decode and log request payload
                decoded_info = self._decode_payload_by_content_type(payload, content_format)
                if decoded_info['format'] == 'cbor':
                    json_payload = json.dumps(decoded_info['decoded'], indent=2, default=str)
                    logger.info("Request payload (CBOR decoded):\n%s", json_payload)
                elif decoded_info['format'] == 'json':
                    json_payload = json.dumps(decoded_info['decoded'], indent=2, default=str)
                    logger.info("Request payload (JSON):\n%s", json_payload)
                elif decoded_info['format'] in ['text', 'link-format']:
                    logger.info(
                        "Request payload (%s): %s",
                        decoded_info['format'], decoded_info['decoded']
                    )

            # Apply OSCORE security if credentials provided                
            if credentials and credentials.master_secret != "":
                await self._apply_oscore(uri, credentials, kid_context)

            # Ensure context exists before sending any request
            await self._ensure_context()
            assert self.context is not None

            # Send request
            response = await self.context.request(request).response

            # Log response
            logger.info("Response code: %s", response.code)
            
            if response.payload:
                logger.info("Response payload (hex): %s", response.payload.hex())
                logger.info("Response payload (size): %s bytes", len(response.payload))

            success = response.code.is_successful()

            return WriteResult(
                success=success,
                payload=response.payload if response.payload else None,
                response_code=response.code,
            )

        except (OSError, RuntimeError) as e:
            logger.error("Error writing resource: %s", e, exc_info=True)
            return WriteResult(success=False)

    async def observe(
        self,
        host: str,
        port: int,
        resource_path: str,
        callback: Callable[[bytes], None],
        credentials: Optional[OSCORECredentials] = None,
        kid_context: Optional[str] = None,
        duration: int = 60
    ):
        """
        Observe a resource on a KNX IoT device.

        Args:
            host: Device IP address
            port: CoAP port (usually 5683)
            resource_path: Resource path (e.g., "/p/o_1_1")
            callback: Function to call when resource changes
            credentials: OSCORE credentials for secure communication
            kid_context: OSCORE Key ID Context (Installation ID) as hex string
            duration: How long to observe in seconds (handled by caller)
        """
        await self._ensure_context()

        try:
            # Build CoAP URI - wrap IPv6 addresses in brackets
            if ':' in host and not host.startswith('['):
                # IPv6 address - needs brackets
                uri = f"coap://[{host}]:{port}{resource_path}"
            else:
                # IPv4 address or already has brackets
                uri = f"coap://{host}:{port}{resource_path}"

            # Create GET request with Observe option
            request = Message(code=Code.GET, uri=uri, observe=0)

            # Apply OSCORE security if credentials provided
            if credentials:
                await self._apply_oscore(uri, credentials, kid_context)

            # Ensure context exists before sending any request
            await self._ensure_context()
            assert self.context is not None

            # Create observation
            observation_request = self.context.request(request)

            try:
                # Get initial response
                response = await observation_request.response

                if response.code.is_successful():
                    # Log initial response payload
                    self._log_response_payload(response, resource_path)
                    callback(response.payload)

                    # Continue observing
                    if observation_request.observation is not None:
                        async for response in observation_request.observation:
                            # Log observation update payload
                            self._log_response_payload(response, resource_path)
                            callback(response.payload)

                            # Check duration
                            # Note: This is simplified - would need proper timeout handling

                else:
                    logger.warning("Observe failed with code: %s", response.code)
                    # Log error response payload
                    self._log_response_payload(response, resource_path)

            except asyncio.TimeoutError:
                logger.debug("Observation completed (timeout)")
            finally:
                try:
                    pass
                except (AssertionError, RuntimeError):
                    # Observation already cancelled
                    pass

        except (OSError, RuntimeError) as e:
            logger.error("Error observing resource: %s", e, exc_info=True)

    async def close(self):
        """Close the CoAP context and cleanup OSCORE resources."""
        # Clean up OSCORE contexts first to release file locks
        for serial_number, security_context in self._oscore_contexts.items():
            try:
                # Call _destroy() to release the lock file before Python garbage collection
                if hasattr(security_context, '_destroy'):
                    security_context._destroy()  # pylint: disable=protected-access
            except (PermissionError, OSError, AttributeError) as e:
                # Ignore cleanup errors on Windows - file locks may still be held
                logger.debug("OSCORE cleanup warning for %s: %s", serial_number, e)

        self._oscore_contexts.clear()

        if self.context:
            await self.context.shutdown()
            self.context = None
