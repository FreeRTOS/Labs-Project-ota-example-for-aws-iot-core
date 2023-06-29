/*
 * AWS IoT Device SDK for Embedded C 202108.00
 * Copyright (C) 2020 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#define LIBRARY_LOG_NAME  "Transport_OpenSSL"
#define LIBRARY_LOG_LEVEL LOG_DEBUG
#include "csdk_logging/logging.h"

/* Standard includes. */
#include <assert.h>
#include <string.h>

/* POSIX socket includes. */
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>

/* Transport interface include. */
#include "transport_interface.h"

#include "openssl_posix.h"
#include <openssl/err.h>
#include <openssl/evp.h>

/*-----------------------------------------------------------*/

/**
 * @brief Add X509 certificate to the trusted list of root certificates.
 *
 * OpenSSL does not provide a single function for reading and loading
 * certificates from files into stores, so the file API must be called. Start
 * with the root certificate.
 *
 * @param[out] pSslContext SSL context to which the trusted server root CA is to
 * be added.
 * @param[in] pRootCaBuffer Filepath string to the trusted server root CA.
 *
 * @return 1 on success; -1, 0 on failure;
 */
static int32_t setRootCa( const SSL_CTX * pSslContext,
                          const char * pRootCaBuffer,
                          int rootCaLength );

/**
 * @brief Set X509 certificate as client certificate for the server to
 * authenticate.
 *
 * @param[out] pSslContext SSL context to which the client certificate is to be
 * set.
 * @param[in] pClientCertBuffer Buffer string to the client certificate.
 *
 * @return 1 on success; 0 failure;
 */
static int32_t setClientCertificate( SSL_CTX * pSslContext,
                                     const char * pClientCertBuffer,
                                     int clientCertLength );

/**
 * @brief Set private key for the client's certificate.
 *
 * @param[out] pSslContext SSL context to which the private key is to be added.
 * @param[in]  pem_key_buffer String to the client private key.
 * @param[in]  pem_key_buffer_len int to the client private key.
 *
 * @return 1 on success; 0 on failure;
 */
static int32_t setPrivateKey( SSL_CTX * pSslContext,
                              const char * pem_key_buffer,
                              int pem_key_buffer_len );

/**
 * @brief Passes TLS credentials to the OpenSSL library.
 *
 * Provides the root CA certificate, client certificate, and private key to the
 * OpenSSL library. If the client certificate or private key is not NULL, mutual
 * authentication is used when performing the TLS handshake.
 *
 * @param[out] pSslContext SSL context to which the credentials are to be
 * imported.
 * @param[in] pOpensslCredentials TLS credentials to be imported.
 *
 * @return 1 on success; -1, 0 on failure;
 */
static int32_t setCredentials(
    SSL_CTX * pSslContext,
    const OpensslCredentials_t * pOpensslCredentials );

/**
 * @brief Set optional configurations for the TLS connection.
 *
 * This function is used to set SNI, MFLN, and ALPN protocols.
 *
 * @param[in] pSsl SSL context to which the optional configurations are to be
 * set.
 * @param[in] pOpensslCredentials TLS credentials containing configurations.
 */
static void setOptionalConfigurations(
    SSL * pSsl,
    const OpensslCredentials_t * pOpensslCredentials );

/**
 * @brief Converts the sockets wrapper status to openssl status.
 *
 * @param[in] socketStatus Sockets wrapper status.
 *
 * @return #OPENSSL_SUCCESS, #OPENSSL_INVALID_PARAMETER, #OPENSSL_DNS_FAILURE,
 * and #OPENSSL_CONNECT_FAILURE.
 */
static OpensslStatus_t convertToOpensslStatus( SocketStatus_t socketStatus );

/**
 * @brief Establish TLS session by performing handshake with the server.
 *
 * @param[in] pServerInfo Server connection info.
 * @param[in] pOpensslParams Parameters to perform the TLS handshake.
 * @param[in] pOpensslCredentials TLS credentials containing configurations.
 *
 * @return #OPENSSL_SUCCESS, #OPENSSL_API_ERROR, and #OPENSSL_HANDSHAKE_FAILED.
 */
static OpensslStatus_t tlsHandshake(
    const ServerInfo_t * pServerInfo,
    OpensslParams_t * pOpensslParams,
    const OpensslCredentials_t * pOpensslCredentials );

/*-----------------------------------------------------------*/

static OpensslStatus_t convertToOpensslStatus( SocketStatus_t socketStatus )
{
    OpensslStatus_t opensslStatus = OPENSSL_INVALID_PARAMETER;

    switch( socketStatus )
    {
        case SOCKETS_SUCCESS:
            opensslStatus = OPENSSL_SUCCESS;
            break;

        case SOCKETS_INVALID_PARAMETER:
            opensslStatus = OPENSSL_INVALID_PARAMETER;
            break;

        case SOCKETS_DNS_FAILURE:
            opensslStatus = OPENSSL_DNS_FAILURE;
            break;

        case SOCKETS_CONNECT_FAILURE:
            opensslStatus = OPENSSL_CONNECT_FAILURE;
            break;

        default:
            LogError( ( "Unexpected status received from socket wrapper: "
                        "Socket status = %u",
                        socketStatus ) );
            break;
    }

    return opensslStatus;
}
/*-----------------------------------------------------------*/

static OpensslStatus_t tlsHandshake(
    const ServerInfo_t * pServerInfo,
    OpensslParams_t * pOpensslParams,
    const OpensslCredentials_t * pOpensslCredentials )
{
    OpensslStatus_t returnStatus = OPENSSL_SUCCESS;
    int32_t sslStatus = -1, verifyPeerCertStatus = X509_V_OK;

    /* Validate the hostname against the server's certificate. */
    sslStatus = SSL_set1_host( pOpensslParams->pSsl, pServerInfo->pHostName );

    if( sslStatus != 1 )
    {
        LogError( ( "SSL_set1_host failed to set the hostname to validate." ) );
        returnStatus = OPENSSL_API_ERROR;
    }

    /* Enable SSL peer verification. */
    if( returnStatus == OPENSSL_SUCCESS )
    {
        SSL_set_verify( pOpensslParams->pSsl, SSL_VERIFY_PEER, NULL );

        /* Setup the socket to use for communication. */
        sslStatus = SSL_set_fd( pOpensslParams->pSsl,
                                pOpensslParams->socketDescriptor );

        if( sslStatus != 1 )
        {
            LogError(
                ( "SSL_set_fd failed to set the socket fd to SSL context." ) );
            returnStatus = OPENSSL_API_ERROR;
        }
    }

    /* Perform the TLS handshake. */
    if( returnStatus == OPENSSL_SUCCESS )
    {
        setOptionalConfigurations( pOpensslParams->pSsl, pOpensslCredentials );

        sslStatus = SSL_connect( pOpensslParams->pSsl );

        if( sslStatus != 1 )
        {
            unsigned long ret;
            ret = SSL_get_error( pOpensslParams->pSsl, sslStatus );
            LogError(
                ( "SSL_connect failed to perform TLS handshake. %lu", ret ) );
            returnStatus = OPENSSL_HANDSHAKE_FAILED;
            if( ret == SSL_ERROR_SYSCALL )
            {
                ret = ERR_get_error();
                while( ret != 0 )
                {
                    ret = ERR_get_error();
                    LogError( ( "Error get error: %s",
                                ERR_error_string( ret, NULL ) ) );
                    ret = ERR_get_error();
                }
            }
        }
    }

    /* Verify X509 certificate from peer. */
    if( returnStatus == OPENSSL_SUCCESS )
    {
        verifyPeerCertStatus = ( int32_t ) SSL_get_verify_result(
            pOpensslParams->pSsl );

        if( verifyPeerCertStatus != X509_V_OK )
        {
            LogError( ( "SSL_get_verify_result failed to verify X509 "
                        "certificate from peer." ) );
            returnStatus = OPENSSL_HANDSHAKE_FAILED;
        }
    }

    return returnStatus;
}

static int32_t setRootCa( const SSL_CTX * pSslContext,
                          const char * pRootCaBuffer,
                          int rootCaLength )
{
    int32_t sslStatus = 1;
    FILE * pRootCaFile = NULL;
    X509 * pRootCa = NULL;
    BIO * bio = NULL;

    assert( pSslContext != NULL );
    assert( pRootCaBuffer != NULL );

    // Create a read-only BIO backed by the supplied memory buffer
    bio = BIO_new_mem_buf( ( void * ) pRootCaBuffer, rootCaLength );

    pRootCa = PEM_read_bio_X509( bio, NULL, NULL, NULL );

    // Cleanup
    BIO_free( bio );

    if( pRootCa == NULL )
    {
        LogError( ( "PEM_read_X509 failed to parse root CA." ) );
        sslStatus = -1;
    }

    if( sslStatus == 1 )
    {
        /* Add the certificate to the context. */
        sslStatus = X509_STORE_add_cert( SSL_CTX_get_cert_store( pSslContext ),
                                         pRootCa );

        if( sslStatus != 1 )
        {
            LogError( ( "X509_STORE_add_cert failed to add root CA to "
                        "certificate store." ) );
            sslStatus = -1;
        }
        /* Free the X509 object used to set the root CA. */
        X509_free( pRootCa );
        pRootCa = NULL;
    }

    /* Close the file if it was successfully opened. */
    if( pRootCaFile != NULL )
    {
        /* MISRA Rule 21.6 flags the following line for using the standard
         * library input/output function `fclose()`. This rule is suppressed
         * because openssl function #PEM_read_X509 takes an argument of type
         * `FILE *` for reading the root ca PEM file and `fopen()` is used to
         * get the file pointer. The file opened with `fopen()` needs to be
         * closed by calling `fclose()`.*/
        /* coverity[misra_c_2012_rule_21_6_violation] */
        if( fclose( pRootCaFile ) != 0 )
        {
            LogWarn( ( "fclose failed to close file." ) );
        }
    }

    /* Log the success message if we successfully imported the root CA. */
    if( sslStatus == 1 )
    {
        LogDebug( ( "Successfully imported root CA." ) );
    }

    return sslStatus;
}
/*-----------------------------------------------------------*/

static int32_t setClientCertificate( SSL_CTX * pSslContext,
                                     const char * pClientCertBuffer,
                                     int clientCertLength )
{
    int32_t sslStatus = -1;

    assert( pSslContext != NULL );
    assert( pClientCertBuffer != NULL );

    BIO * bio;
    X509 * clientCert;
    bio = BIO_new_mem_buf( ( void * ) pClientCertBuffer, clientCertLength );

    clientCert = PEM_read_bio_X509( bio, NULL, NULL, NULL );
    sslStatus = SSL_CTX_use_certificate( pSslContext, clientCert );

    if( sslStatus != 1 )
    {
        LogError( ( "SSL_CTX_use_certificate_chain failed to import "
                    "client certificate" ) );
    }
    else
    {
        LogDebug( ( "Successfully imported client certificate." ) );
    }
    BIO_free( bio );
    X509_free( clientCert );
    return sslStatus;
}
/*-----------------------------------------------------------*/

static int32_t setPrivateKey( SSL_CTX * pSslContext,
                              const char * pem_key_buffer,
                              int pem_key_buffer_len )
{
    int32_t sslStatus = -1;

    assert( pSslContext != NULL );
    assert( pem_key_buffer != NULL );
    assert( pem_key_buffer_len != 0 );

    BIO * bufio;
    EVP_PKEY * key;

    bufio = BIO_new_mem_buf( ( void * ) pem_key_buffer, pem_key_buffer_len );
    key = PEM_read_bio_PrivateKey( bufio, NULL, 0, NULL );

    /* Import the client certificate private key. */
    sslStatus = SSL_CTX_use_PrivateKey( pSslContext, key );

    if( sslStatus != 1 )
    {
        LogError( ( "SSL_CTX_use_PrivateKey failed to import client "
                    "certificate private key" ) );
    }
    else
    {
        LogDebug( ( "Successfully imported client certificate private key." ) );
    }

    BIO_free( bufio );
    EVP_PKEY_free( key );

    return sslStatus;
}
/*-----------------------------------------------------------*/

static int32_t setCredentials( SSL_CTX * pSslContext,
                               const OpensslCredentials_t * pOpensslCredentials )
{
    int32_t sslStatus = 0;

    assert( pSslContext != NULL );
    assert( pOpensslCredentials != NULL );

    if( pOpensslCredentials->pRootCaBuffer != NULL )
    {
        sslStatus = setRootCa( pSslContext,
                               pOpensslCredentials->pRootCaBuffer,
                               pOpensslCredentials->rootCaLength );
    }

    if( ( sslStatus == 1 ) &&
        ( pOpensslCredentials->pClientCertBuffer != NULL ) )
    {
        sslStatus = setClientCertificate(
            pSslContext,
            pOpensslCredentials->pClientCertBuffer,
            pOpensslCredentials->clientCertLength );
    }

    if( ( sslStatus == 1 ) &&
        ( pOpensslCredentials->pPrivateKeyBuffer != NULL ) )
    {
        sslStatus = setPrivateKey( pSslContext,
                                   pOpensslCredentials->pPrivateKeyBuffer,
                                   pOpensslCredentials->privateKeyLength );
    }

    return sslStatus;
}
/*-----------------------------------------------------------*/

static void setOptionalConfigurations(
    SSL * pSsl,
    const OpensslCredentials_t * pOpensslCredentials )
{
    int32_t sslStatus = -1;
    int16_t readBufferLength = 0;

    assert( pSsl != NULL );
    assert( pOpensslCredentials != NULL );

    /* Set TLS ALPN if requested. */
    if( ( pOpensslCredentials->pAlpnProtos != NULL ) &&
        ( pOpensslCredentials->alpnProtosLen > 0U ) )
    {
        LogDebug( ( "Setting ALPN protos." ) );
        sslStatus = SSL_set_alpn_protos(
            pSsl,
            ( const uint8_t * ) pOpensslCredentials->pAlpnProtos,
            ( uint32_t ) pOpensslCredentials->alpnProtosLen );

        if( sslStatus != 0 )
        {
            LogError( ( "SSL_set_alpn_protos failed to set ALPN protos. %s",
                        pOpensslCredentials->pAlpnProtos ) );
        }
    }

    /* Set TLS MFLN if requested. */
    if( pOpensslCredentials->maxFragmentLength > 0U )
    {
        LogDebug( ( "Setting max send fragment length %u.",
                    pOpensslCredentials->maxFragmentLength ) );

        /* Set the maximum send fragment length. */

        /* MISRA Directive 4.6 flags the following line for using basic
         * numerical type long. This directive is suppressed because openssl
         * function #SSL_set_max_send_fragment expects a length argument
         * type of long. */
        /* coverity[misra_c_2012_directive_4_6_violation] */
        sslStatus = ( int32_t ) SSL_set_max_send_fragment(
            pSsl,
            ( long ) pOpensslCredentials->maxFragmentLength );

        if( sslStatus != 1 )
        {
            LogError( ( "Failed to set max send fragment length %u.",
                        pOpensslCredentials->maxFragmentLength ) );
        }
        else
        {
            readBufferLength = ( int16_t )
                                   pOpensslCredentials->maxFragmentLength +
                               SSL3_RT_MAX_ENCRYPTED_OVERHEAD;

            /* Change the size of the read buffer to match the
             * maximum fragment length + some extra bytes for overhead. */
            SSL_set_default_read_buffer_len( pSsl,
                                             ( size_t ) readBufferLength );
        }
    }

    /* Enable SNI if requested. */
    if( pOpensslCredentials->sniHostName != NULL )
    {
        LogDebug( ( "Setting server name %s for SNI.",
                    pOpensslCredentials->sniHostName ) );

        /* MISRA Rule 11.8 flags the following line for removing the const
         * qualifier from the pointed to type. This rule is suppressed because
         * openssl implementation of #SSL_set_tlsext_host_name internally casts
         * the pointer to a string literal to a `void *` pointer. */
        /* coverity[misra_c_2012_rule_11_8_violation] */
        sslStatus = ( int32_t )
            SSL_set_tlsext_host_name( pSsl, pOpensslCredentials->sniHostName );

        if( sslStatus != 1 )
        {
            LogError( ( "Failed to set server name %s for SNI.",
                        pOpensslCredentials->sniHostName ) );
        }
    }
}
/*-----------------------------------------------------------*/

OpensslStatus_t Openssl_Connect(
    NetworkContext_t * pNetworkContext,
    const ServerInfo_t * pServerInfo,
    const OpensslCredentials_t * pOpensslCredentials,
    uint32_t sendTimeoutMs,
    uint32_t recvTimeoutMs )
{
    OpensslParams_t * pOpensslParams = NULL;
    SocketStatus_t socketStatus = SOCKETS_SUCCESS;
    OpensslStatus_t returnStatus = OPENSSL_SUCCESS;
    int32_t sslStatus = 0;
    uint8_t sslObjectCreated = 0;
    SSL_CTX * pSslContext = NULL;

    sigset_t old_set;
    sigset_t set;

    sigfillset( &set );
    sigdelset( &set, SIGINT );  // allow reception of ctrl_c
    sigdelset( &set, SIGTRAP ); // allow reception of debugger trap
    sigdelset( &set, SIGSTOP ); // allow reception of debugger stop

    /* Validate parameters. */
    if( ( pNetworkContext == NULL ) || ( pNetworkContext->pParams == NULL ) )
    {
        LogError( ( "Parameter check failed: pNetworkContext is NULL." ) );
        returnStatus = OPENSSL_INVALID_PARAMETER;
    }
    else if( pOpensslCredentials == NULL )
    {
        LogError( ( "Parameter check failed: pOpensslCredentials is NULL." ) );
        returnStatus = OPENSSL_INVALID_PARAMETER;
    }
    else
    {
        /* Empty else. */
    }

    pthread_sigmask( SIG_SETMASK, &set, &old_set );
    /* Establish the TCP connection. */
    if( returnStatus == OPENSSL_SUCCESS )
    {
        pOpensslParams = pNetworkContext->pParams;
        socketStatus = Sockets_Connect( &pOpensslParams->socketDescriptor,
                                        pServerInfo,
                                        sendTimeoutMs,
                                        recvTimeoutMs );

        /* Convert socket wrapper status to openssl status. */
        returnStatus = convertToOpensslStatus( socketStatus );
    }

    /* Create SSL context. */
    if( returnStatus == OPENSSL_SUCCESS )
    {
        pSslContext = SSL_CTX_new( TLS_client_method() );

        if( pSslContext == NULL )
        {
            LogError( ( "Creation of a new SSL_CTX object failed." ) );
            returnStatus = OPENSSL_API_ERROR;
        }
    }

    /* Setup credentials. */
    if( returnStatus == OPENSSL_SUCCESS )
    {
        /* Enable partial writes for blocking calls to SSL_write to allow a
         * payload larger than the maximum fragment length.
         * The mask returned by SSL_CTX_set_mode does not need to be checked. */

        /* MISRA Directive 4.6 flags the following line for using basic
         * numerical type long. This directive is suppressed because openssl
         * function #SSL_CTX_set_mode takes an argument of type long. */
        /* coverity[misra_c_2012_directive_4_6_violation] */
        ( void ) SSL_CTX_set_mode( pSslContext,
                                   ( long ) SSL_MODE_ENABLE_PARTIAL_WRITE );

        sslStatus = setCredentials( pSslContext, pOpensslCredentials );

        if( sslStatus != 1 )
        {
            LogError( ( "Setting up credentials failed." ) );
            returnStatus = OPENSSL_INVALID_CREDENTIALS;
        }
    }

    /* Create a new SSL session. */
    if( returnStatus == OPENSSL_SUCCESS )
    {
        pOpensslParams->pSsl = SSL_new( pSslContext );

        if( pOpensslParams->pSsl == NULL )
        {
            LogError( ( "SSL_new failed to create a new SSL context." ) );
            returnStatus = OPENSSL_API_ERROR;
        }
        else
        {
            sslObjectCreated = 1u;
        }
    }

    /* Setup the socket to use for communication. */
    if( returnStatus == OPENSSL_SUCCESS )
    {
        returnStatus = tlsHandshake( pServerInfo,
                                     pOpensslParams,
                                     pOpensslCredentials );
    }

    /* Free the SSL context. */
    if( pSslContext != NULL )
    {
        SSL_CTX_free( pSslContext );
        pSslContext = NULL;
    }

    /* Clean up on error. */
    if( ( returnStatus != OPENSSL_SUCCESS ) && ( sslObjectCreated == 1u ) )
    {
        SSL_free( pOpensslParams->pSsl );
        pOpensslParams->pSsl = NULL;
    }

    /* Log failure or success depending on status. */
    if( returnStatus != OPENSSL_SUCCESS )
    {
        LogError( ( "Failed to establish a TLS connection." ) );
    }
    else
    {
        LogDebug( ( "Established a TLS connection." ) );
    }
    pthread_sigmask( SIG_SETMASK, &old_set, NULL );

    return returnStatus;
}
/*-----------------------------------------------------------*/

OpensslStatus_t Openssl_Disconnect( const NetworkContext_t * pNetworkContext )
{
    OpensslParams_t * pOpensslParams = NULL;
    SocketStatus_t socketStatus = SOCKETS_INVALID_PARAMETER;

    sigset_t set;
    sigset_t old_set;

    sigfillset( &set );
    sigdelset( &set, SIGINT );  // allow reception of ctrl_c
    sigdelset( &set, SIGTRAP ); // allow reception of debugger trap
    sigdelset( &set, SIGSTOP ); // allow reception of debugger stop

    if( ( pNetworkContext == NULL ) || ( pNetworkContext->pParams == NULL ) )
    {
        /* No need to update the status here. The socket status
         * SOCKETS_INVALID_PARAMETER will be converted to openssl
         * status OPENSSL_INVALID_PARAMETER before returning from this
         * function. */
        LogError( ( "Parameter check failed: pNetworkContext is NULL." ) );
    }
    else
    {
        pthread_sigmask( SIG_SETMASK, &set, &old_set );
        pOpensslParams = pNetworkContext->pParams;

        if( pOpensslParams->pSsl != NULL )
        {
            /* SSL shutdown should be called twice: once to send "close notify"
             * and once more to receive the peer's "close notify". */
            if( SSL_shutdown( pOpensslParams->pSsl ) == 0 )
            {
                ( void ) SSL_shutdown( pOpensslParams->pSsl );
            }

            SSL_free( pOpensslParams->pSsl );
            pOpensslParams->pSsl = NULL;
        }

        /* Tear down the socket connection, pNetworkContext != NULL here. */
        socketStatus = Sockets_Disconnect( pOpensslParams->socketDescriptor );
        pthread_sigmask( SIG_SETMASK, &old_set, NULL );
    }

    return convertToOpensslStatus( socketStatus );
}
/*-----------------------------------------------------------*/

/* MISRA Rule 8.13 flags the following line for not using the const qualifier
 * on `pNetworkContext`. Indeed, the object pointed by it is not modified
 * by OpenSSL, but other implementations of `TransportRecv_t` may do so. */
int32_t Openssl_Recv( NetworkContext_t * pNetworkContext,
                      void * pBuffer,
                      size_t bytesToRecv )
{
    OpensslParams_t * pOpensslParams = NULL;
    int32_t bytesReceived = 0;
    sigset_t set;
    sigset_t old_set;

    sigfillset( &set );
    sigdelset( &set, SIGINT );  // allow reception of ctrl_c
    sigdelset( &set, SIGTRAP ); // allow reception of debugger trap
    sigdelset( &set, SIGSTOP ); // allow reception of debugger stop

    pthread_sigmask( SIG_SETMASK, &set, &old_set );

    if( ( pNetworkContext == NULL ) || ( pNetworkContext->pParams == NULL ) )
    {
        LogError( ( "Parameter check failed: pNetworkContext is NULL." ) );
        bytesReceived = -1;
    }
    else if( pNetworkContext->pParams->pSsl == NULL )
    {
        LogError( ( "Failed to receive data over network: "
                    "SSL object in network context is NULL." ) );
        bytesReceived = -1;
    }
    else
    {
        int32_t pollStatus = 1, readStatus = 1, sslError = 0;
        uint8_t shouldRead = 0U;
        struct pollfd pollFds;
        pOpensslParams = pNetworkContext->pParams;

        /* Initialize the file descriptor.
         * #POLLPRI corresponds to high-priority data while #POLLIN corresponds
         * to any other data that may be read. */
        pollFds.events = POLLIN | POLLPRI;
        pollFds.revents = 0;
        /* Set the file descriptor for poll. */
        pollFds.fd = pOpensslParams->socketDescriptor;

        /* #SSL_pending returns a value > 0 if application data
         * from the last processed TLS record remains to be read.
         * This implementation will ALWAYS block when the number of bytes
         * requested is greater than 1. Otherwise, poll the socket first
         * as blocking may negatively impact performance by waiting for the
         * entire duration of the socket timeout even when no data is available.
         */
        if( ( bytesToRecv > 1 ) || ( SSL_pending( pOpensslParams->pSsl ) > 0 ) )
        {
            shouldRead = 1U;
        }
        else
        {
            /* Speculative read for the start of a payload.
             * Note: This is done to avoid blocking when no
             * data is available to be read from the socket. */
            pollStatus = poll( &pollFds, 1, 0 );
        }

        if( pollStatus < 0 )
        {
            bytesReceived = -1;
        }
        else if( pollStatus == 0 )
        {
            /* No data available to be read from the socket. */
            bytesReceived = 0;
        }
        else
        {
            shouldRead = 1U;
        }

        if( shouldRead == 1U )
        {
            /* Blocking SSL read of data.
             * Note: The TLS record may only be partially received or
             * unprocessed, so it is possible that no processed application data
             * is returned
             * even though the socket has data available to be read. */
            readStatus = ( int32_t ) SSL_read( pOpensslParams->pSsl,
                                               pBuffer,
                                               ( int32_t ) bytesToRecv );

            /* Successfully read of application data. */
            if( readStatus > 0 )
            {
                bytesReceived = readStatus;
            }
        }

        /* Handle error return status if transport read did not succeed. */
        if( readStatus <= 0 )
        {
            sslError = SSL_get_error( pOpensslParams->pSsl, readStatus );

            if( sslError == SSL_ERROR_WANT_READ )
            {
                /* The OpenSSL documentation mentions that SSL_Read can provide
                 * a return code of SSL_ERROR_WANT_READ in blocking mode, if the
                 * SSL context is not configured with the SSL_MODE_AUTO_RETRY.
                 * This error code means that the SSL_read() operation needs to
                 * be retried to complete the read operation. Thus, setting the
                 * return value of this function as zero to represent that no
                 * data was received from the network. */
                bytesReceived = 0;
            }
            else
            {
                LogError(
                    ( "Failed to receive data over network: SSL_read failed: "
                      "ErrorStatus=%s.",
                      ERR_reason_error_string( sslError ) ) );

                /* The transport interface requires zero return code only when
                 * the receive operation can be retried to achieve success.
                 * Thus, convert a zero error code to a negative return value as
                 * this cannot be retried. */
                bytesReceived = -1;
            }
        }
    }

    pthread_sigmask( SIG_SETMASK, &old_set, NULL );
    return bytesReceived;
}
/*-----------------------------------------------------------*/

/* MISRA Rule 8.13 flags the following line for not using the const qualifier
 * on `pNetworkContext`. Indeed, the object pointed by it is not modified
 * by OpenSSL, but other implementations of `TransportSend_t` may do so. */
int32_t Openssl_Send( NetworkContext_t * pNetworkContext,
                      const void * pBuffer,
                      size_t bytesToSend )
{
    OpensslParams_t * pOpensslParams = NULL;
    int32_t bytesSent = 0;

    sigset_t set;
    sigset_t old_set;

    sigfillset( &set );
    sigdelset( &set, SIGINT );  // allow reception of ctrl_c
    sigdelset( &set, SIGTRAP ); // allow reception of debugger trap
    sigdelset( &set, SIGSTOP ); // allow reception of debugger stop

    pthread_sigmask( SIG_SETMASK, &set, &old_set );
    if( ( pNetworkContext == NULL ) || ( pNetworkContext->pParams == NULL ) )
    {
        LogError( ( "Parameter check failed: pNetworkContext is NULL." ) );
        bytesSent = -1; // No point retrying here
    }
    else if( pNetworkContext->pParams->pSsl != NULL )
    {
        struct pollfd pollFds;
        int32_t pollStatus;

        pOpensslParams = pNetworkContext->pParams;

        /* Initialize the file descriptor. */
        pollFds.events = POLLOUT;
        pollFds.revents = 0;
        /* Set the file descriptor for poll. */
        pollFds.fd = pOpensslParams->socketDescriptor;

        /* `poll` checks if the socket is ready to send data.
         * Note: This is done to avoid blocking on SSL_write()
         * when TCP socket is not ready to accept more data for
         * network transmission (possibly due to a full TX buffer). */
        pollStatus = poll( &pollFds, 1, 0 );

        if( pollStatus > 0 )
        {
            /* SSL write of data. */
            bytesSent = ( int32_t ) SSL_write( pOpensslParams->pSsl,
                                               pBuffer,
                                               ( int32_t ) bytesToSend );

            if( bytesSent <= 0 )
            {
                LogError( ( "Failed to send data over network: SSL_write of "
                            "OpenSSL failed: "
                            "ErrorStatus=%s.",
                            ERR_reason_error_string(
                                SSL_get_error( pOpensslParams->pSsl,
                                               bytesSent ) ) ) );

                /* As the SSL context is configured for blocking mode, the
                 * SSL_write() function does not return an SSL_ERROR_WANT_READ
                 * or SSL_ERROR_WANT_WRITE error code. The SSL_ERROR_WANT_READ
                 * and SSL_ERROR_WANT_WRITE error codes signify that the write
                 * operation can be retried. However, in the blocking mode, as
                 * the SSL_write() function does not return either of the error
                 * codes, we cannot retry the operation on failure, and thus,
                 * this function will never return a zero error code.
                 */

                /* The transport interface requires zero return code only when
                 * the send operation can be retried to achieve success. Thus,
                 * convert a zero
                 * error code to a negative return value as this cannot be
                 * retried. */
                if( bytesSent == 0 )
                {
                    bytesSent = -1;
                }
            }
        }
        else if( pollStatus < 0 )
        {
            /* An error occurred while polling. */
            LogError( ( "Unable to send TLS data on network: "
                        "An error occurred while checking availability of TCP "
                        "socket %d.",
                        pOpensslParams->socketDescriptor ) );
            bytesSent = -1;
        }
        else
        {
            /* Socket is not available for sending data. Set return code for
             * retrying send. */
            bytesSent = 0;
        }
    }
    else
    {
        LogError( ( "Failed to send data over network: "
                    "SSL object in network context is NULL." ) );
        // In this case retries are futile, so signal that
        bytesSent = -1;
    }

    pthread_sigmask( SIG_SETMASK, &old_set, NULL );
    return bytesSent;
}
/*-----------------------------------------------------------*/
