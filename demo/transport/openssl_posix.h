/*
 * Copyright Amazon.com, Inc. and its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: MIT
 *
 * Licensed under the MIT License. See the LICENSE accompanying this file
 * for the specific language governing permissions and limitations under
 * the License.
 */

#ifndef OPENSSL_POSIX_H_
#define OPENSSL_POSIX_H_

/* *INDENT-OFF* */
#ifdef __cplusplus
extern "C" {
#endif
/* *INDENT-ON* */

/* OpenSSL include. */
#include <openssl/ssl.h>

/* Transport includes. */
#include "transport_interface.h"

/* Socket include. */
#include "sockets_posix.h"

/**
 * @brief Parameters for the transport-interface
 * implementation that uses OpenSSL and POSIX sockets.
 *
 * @note For this transport implementation, the socket descriptor and
 * SSL context is used.
 */
typedef struct OpensslParams
{
    int32_t socketDescriptor;
    SSL * ssl;
} OpensslParams_t;

/* Each compilation unit must define the NetworkContext struct. */
struct NetworkContext
{
    OpensslParams_t * params;
};

/**
 * @brief OpenSSL Connect / Disconnect return status.
 */
typedef enum OpensslStatus
{
    OPENSSL_SUCCESS = 0,         /**< Function successfully completed. */
    OPENSSL_INVALID_PARAMETER,   /**< At least one parameter was invalid. */
    OPENSSL_INSUFFICIENT_MEMORY, /**< Insufficient memory required to
                                    establish connection. */
    OPENSSL_INVALID_CREDENTIALS, /**< Provided credentials were invalid. */
    OPENSSL_HANDSHAKE_FAILED,    /**< Performing TLS handshake with server
                                    failed. */
    OPENSSL_API_ERROR,      /**< A call to a system API resulted in an internal
                               error. */
    OPENSSL_DNS_FAILURE,    /**< Resolving hostname of the server failed. */
    OPENSSL_CONNECT_FAILURE /**< Initial connection to the server failed. */
} OpensslStatus_t;

/**
 * @brief Contains the credentials to establish a TLS connection.
 */
typedef struct OpensslCredentials
{
    /**
     * @brief An array of ALPN protocols. Set to NULL to disable ALPN.
     *
     * See [this link]
     * (https://aws.amazon.com/blogs/iot/mqtt-with-tls-client-authentication-on-port-443-why-it-is-useful-and-how-it-works/)
     * for more information.
     */
    const char * alpnProtos;

    /**
     * @brief Length of the ALPN protocols array.
     */
    uint32_t alpnProtosLen;

    /**
     * @brief Set a host name to enable SNI. Set to NULL to disable SNI.
     *
     * @note This string must be NULL-terminated because the OpenSSL API
     * requires it to be.
     */
    const char * sniHostName;

    /**
     * @brief Set the value for the TLS max fragment length (TLS MFLN)
     *
     * OpenSSL allows this value to be in the range of:
     * [512, 16384 (SSL3_RT_MAX_PLAIN_LENGTH)]
     *
     * @note By setting this to 0, OpenSSL uses the default value,
     * which is 16384 (SSL3_RT_MAX_PLAIN_LENGTH).
     */
    uint16_t maxFragmentLength;

    /**
     * @brief Filepaths to certificates and private key that are used when
     * performing the TLS handshake.
     *
     * @note These strings must be NULL-terminated because the OpenSSL API
     * requires them to be.
     */
    const char * rootCaBuffer;     /**< @brief string to the trusted server
                                       root CA. */
    const char * clientCertBuffer; /**< @brief string to the client
                                       certificate. */
    const char * privateKeyBuffer; /**< @brief string to the client
                                       certificate's private key. */

    int rootCaLength;     /**< @brief size o the trusted server root CA. */
    int clientCertLength; /**< @brief size to the client certificate. */
    int privateKeyLength; /**< @brief  size to the client certificate's
                             private key. */

} OpensslCredentials_t;

/**
 * @brief Sets up a TLS session on top of a TCP connection using the OpenSSL
 * API.
 *
 * @param[out] networkContext The output parameter to return the created
 * network context.
 * @param[in] serverInfo Server connection info.
 * @param[in] opensslCredentials Credentials for the TLS connection.
 * @param[in] sendTimeoutMs Timeout for transport send.
 * @param[in] recvTimeoutMs Timeout for transport recv.
 *
 * @note A timeout of 0 means infinite timeout.
 *
 * @return #OPENSSL_SUCCESS on success;
 * #OPENSSL_INVALID_PARAMETER, #OPENSSL_INVALID_CREDENTIALS,
 * #OPENSSL_INVALID_CREDENTIALS, #OPENSSL_SYSTEM_ERROR on failure.
 */
OpensslStatus_t Openssl_Connect( NetworkContext_t * networkContext,
                                 const ServerInfo_t * serverInfo,
                                 const OpensslCredentials_t * opensslCredentials,
                                 uint32_t sendTimeoutMs,
                                 uint32_t recvTimeoutMs );

/**
 * @brief Closes a TLS session on top of a TCP connection using the OpenSSL
 * API.
 *
 * @param[out] networkContext The output parameter to end the TLS session
 * and clean the created network context.
 *
 * @return #OPENSSL_SUCCESS on success; #OPENSSL_INVALID_PARAMETER on
 * failure.
 */
OpensslStatus_t Openssl_Disconnect( const NetworkContext_t * networkContext );

/**
 * @brief Receives data over an established TLS session using the OpenSSL
 * API.
 *
 * This can be used as #TransportInterface.recv function for receiving data
 * from the network.
 *
 * @param[in] networkContext The network context created using
 * Openssl_Connect API.
 * @param[out] buffer Buffer to receive network data into.
 * @param[in] bytesToRecv Number of bytes requested from the network.
 *
 * @return Number of bytes received if successful; negative value to
 * indicate failure. A return value of zero represents that the receive
 * operation can be retried.
 */
int32_t Openssl_Recv( NetworkContext_t * networkContext,
                      void * buffer,
                      size_t bytesToRecv );

/**
 * @brief Sends data over an established TLS session using the OpenSSL API.
 *
 * This can be used as the #TransportInterface.send function to send data
 * over the network.
 *
 * @param[in] networkContext The network context created using
 * Openssl_Connect API.
 * @param[in] buffer Buffer containing the bytes to send over the network
 * stack.
 * @param[in] bytesToSend Number of bytes to send over the network.
 *
 * @return Number of bytes sent if successful; negative value on error.
 *
 * @note This function does not return zero value because it cannot be
 * retried on send operation failure.
 */
int32_t Openssl_Send( NetworkContext_t * networkContext,
                      const void * buffer,
                      size_t bytesToSend );

/* *INDENT-OFF* */
#ifdef __cplusplus
}
#endif
/* *INDENT-ON* */

#endif /* ifndef OPENSSL_POSIX_H_ */
