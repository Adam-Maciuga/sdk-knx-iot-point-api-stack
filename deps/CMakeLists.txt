cmake_minimum_required (VERSION 3.21)

# Load module to fetch external content.
include(FetchContent)

### TinyCBOR (provided by the KNX-IoT stack) ###
set(CBOR_SOURCES
     ${CMAKE_CURRENT_SOURCE_DIR}/tinycbor/src/cborencoder.c
     ${CMAKE_CURRENT_SOURCE_DIR}/tinycbor/src/cborencoder_close_container_checked.c
     ${CMAKE_CURRENT_SOURCE_DIR}/tinycbor/src/cborerrorstrings.c
     ${CMAKE_CURRENT_SOURCE_DIR}/tinycbor/src/cborparser.c
     ${CMAKE_CURRENT_SOURCE_DIR}/tinycbor/src/cborpretty.c
)

add_library(tinycbor-master ${CBOR_SOURCES})

target_include_directories(tinycbor-master PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/tinycbor/src)

### Mbed TLS (provided by the KNX-IoT stack) ###
if(OC_USE_KNX_MBEDTLS)
    # Mbed TLS version is defined in git .submodules file, see repository(3.6.4)
    # TBD FIXME There is no .submodules file, only .gitmodules. As it looks that one doesn't take a specific submodule version.
    # TBD FIXME Updated to version 3.6.5 (LTS security update)
    # $ git checkout tags/mbedtls-3.6.5

    set(TLS_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/debug.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/mps_reader.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/mps_trace.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/net_sockets.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/ssl_cache.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/ssl_ciphersuites.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/ssl_client.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/ssl_cookie.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/ssl_debug_helpers_generated.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/ssl_msg.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/ssl_ticket.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/ssl_tls.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/ssl_tls12_client.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/ssl_tls12_server.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/ssl_tls13_keys.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/ssl_tls13_server.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/ssl_tls13_client.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/ssl_tls13_generic.c
    )
    
    set(X509_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/pkcs7.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/x509.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/x509_create.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/x509_crl.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/x509_crt.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/x509_csr.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/x509write.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/x509write_crt.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/x509write_csr.c
    )
    
    set(CRYPTO_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/aes.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/aesni.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/aesce.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/aria.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/asn1parse.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/asn1write.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/base64.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/bignum.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/bignum_core.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/bignum_mod.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/bignum_mod_raw.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/block_cipher.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/camellia.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/ccm.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/chacha20.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/chachapoly.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/cipher.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/cipher_wrap.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/constant_time.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/cmac.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/ctr_drbg.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/des.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/dhm.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/ecdh.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/ecdsa.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/ecjpake.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/ecp.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/ecp_curves.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/ecp_curves_new.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/entropy.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/entropy_poll.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/error.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/gcm.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/hkdf.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/hmac_drbg.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/lmots.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/lms.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/md.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/md5.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/memory_buffer_alloc.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/nist_kw.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/oid.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/padlock.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/pem.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/pk.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/pk_ecc.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/pk_wrap.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/pkcs12.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/pkcs5.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/pkparse.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/pkwrite.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/platform.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/platform_util.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/poly1305.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/psa_crypto.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/psa_crypto_aead.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/psa_crypto_cipher.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/psa_crypto_client.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/psa_crypto_driver_wrappers_no_static.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/psa_crypto_ecp.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/psa_crypto_ffdh.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/psa_crypto_hash.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/psa_crypto_mac.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/psa_crypto_pake.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/psa_crypto_rsa.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/psa_crypto_se.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/psa_crypto_slot_management.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/psa_crypto_storage.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/psa_its_file.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/psa_util.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/ripemd160.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/rsa.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/rsa_alt_helpers.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/sha1.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/sha256.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/sha512.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/sha3.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/threading.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/timing.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/version.c
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/library/version_features.c
    )

    # Integrate Mbed TLS manually (lean build process) and not with cmake 'add_subdirectory' functionality (directly the mbedtls cmake file)
    add_library(mbedtls ${TLS_SOURCES})
    add_library(mbedx509 ${X509_SOURCES})
    add_library(mbedcrypto ${CRYPTO_SOURCES})

    # When using KNX standard security source MBTLS -> use config definitions defined by Intel
    target_compile_definitions(mbedcrypto PUBLIC MBEDTLS_CONFIG_FILE=\"knx_mbedtls_config.h\")

    # TBD FIXME why is ${PROJECT_SOURCE_DIR}, ${PROJECT_SOURCE_DIR}/include and ${PORT_DIR} needed?
    # TBD FIXME #include <oc_config.h>,  #include "port/oc_connectivity.h", #include "port/oc_assert.h" why are those needed!
    # Note:
    # knx_mbedtls_config.h is in ${PROJECT_SOURCE_DIR}/security
    target_include_directories(mbedcrypto PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/include
        ${PROJECT_SOURCE_DIR}/security
        ${PROJECT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/include
        ${PORT_DIR}
    )

    if(WIN32)
        set(libs ws2_32 bcrypt) # ensure loading WIN32 socket DLL and bcrypt DLL
    endif()

    target_link_libraries(mbedcrypto ${libs})
    target_link_libraries(mbedx509 mbedcrypto)
    target_link_libraries(mbedtls mbedx509)

    set_target_properties(mbedcrypto PROPERTIES OUTPUT_NAME mbedcrypto)
    set_target_properties(mbedx509 PROPERTIES OUTPUT_NAME mbedx509)
    set_target_properties(mbedtls PROPERTIES OUTPUT_NAME mbedtls)

    install(TARGETS mbedcrypto mbedx509 mbedtls
            PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
endif()
