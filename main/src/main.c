/*
 * Copyright Amazon.com, Inc. and its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: MIT
 *
 * Licensed under the MIT License. See the LICENSE accompanying this file
 * for the specific language governing permissions and limitations under
 * the License.
 */

#include <string.h>

#include "FreeRTOS.h"
#include "credentials/credentials_INSECURE.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "task.h"
#include "wifi/wifi.h"

#include "core_mqtt.h"
#include "mqtt_wrapper.h"
#include "ota_demo/ota_demo.h"
#include "transport/transport.h"

static StaticSemaphore_t MQTTAgentLockBuffer;
static StaticSemaphore_t MQTTStateUpdateLockBuffer;
SemaphoreHandle_t MQTTAgentLock = NULL;
SemaphoreHandle_t MQTTStateUpdateLock = NULL;

static MQTTContext_t mqttContext = { 0 };
static uint8_t networkBuffer[ 5000U ] = { 0 };
static TransportInterface_t transport = { 0 };

void mainTask( void * pvParamters );

void PubSubTask( void * pvParameters );

static uint32_t getTimeMs( void );

static void mqttEventCallback( MQTTContext_t * mqttContext,
                               MQTTPacketInfo_t * packetInfo,
                               MQTTDeserializedInfo_t * deserializedInfo );

static void handleIncomingMQTTMessage( char * topic,
                                       size_t topicLength,
                                       uint8_t * message,
                                       size_t messageLength );

int app_main( void )
{
    MQTTFixedBuffer_t fixedBuffer = { 0 };
    MQTTStatus_t mqttResult;
    nvs_flash_init();
    pfWifi_init();

    MQTTAgentLock = xSemaphoreCreateRecursiveMutexStatic(
        &MQTTAgentLockBuffer );
    MQTTStateUpdateLock = xSemaphoreCreateMutexStatic(
        &MQTTStateUpdateLockBuffer );

    fixedBuffer.pBuffer = networkBuffer;
    fixedBuffer.size = 5000U;

    pfTransport_tlsInit( &transport );
    mqttResult = MQTT_Init( &mqttContext,
                            &transport,
                            getTimeMs,
                            mqttEventCallback,
                            &fixedBuffer );
    assert( mqttResult == MQTTSuccess );

    char thingName[ 128U + 1 ] = { 0 };
    size_t thingNameSize = 129U;
    pfCred_getThingName( thingName, &thingNameSize );

    mqttWrapper_setCoreMqttContext( &mqttContext );
    mqttWrapper_setThingName( thingName );

    pfWifi_startNetwork();

    xTaskCreate( mainTask, "MAIN", 6000, NULL, 6, NULL );
    xTaskCreate( PubSubTask, "PUBSUB", 6000, NULL, 5, NULL );

    return 0;
}

void mainTask( void * pvParamters )
{
    ( void ) pvParamters;

    ESP_LOGE( "MAIN", "Main task started" );
    char endpoint[ 256U ] = { 0 };
    size_t endpointLength = 256U;
    pfCred_getEndpoint( endpoint, &endpointLength );
    vTaskDelay( pdMS_TO_TICKS( 100 ) );

    bool result = pfTransport_tlsConnect( endpoint, endpointLength );
    assert( result );
    result = mqttWrapper_connect();
    assert( result );
    ESP_LOGE( "MAIN", "Connected to IoT Core" );

    otaDemo_start();

    while( true )
    {
        // TODO: Make the application which will happen alongside OTA
        // Yield so the idle will get a few cycles and the WDT will be avoided
        MQTT_ProcessLoop( mqttWrapper_getCoreMqttContext() );
        vTaskDelay( pdMS_TO_TICKS( 10 ) );
    }
}

void PubSubTask( void * pvParameters )
{
    ESP_LOGE( "PUBSUB", "PubSub task started" );

    ( void ) pvParameters;
    char * topic = "Hello";
    size_t topicLength = 6;

    while( true )
    {
        mqttWrapper_subscribe(topic, topicLength);
        mqttWrapper_publish(topic, topicLength, ( uint8_t * ) "hello world", 12);
        vTaskDelay( pdMS_TO_TICKS( 500 ) );
        mqttWrapper_unsubscribe(topic, topicLength);
    }
}

static void mqttEventCallback( MQTTContext_t * mqttContext,
                               MQTTPacketInfo_t * packetInfo,
                               MQTTDeserializedInfo_t * deserializedInfo )
{
    char * topic = NULL;
    size_t topicLength = 0U;
    uint8_t * message = NULL;
    size_t messageLength = 0U;

    ( void ) mqttContext;

    if( ( packetInfo->type & 0xF0U ) == MQTT_PACKET_TYPE_PUBLISH )
    {
        assert( deserializedInfo->pPublishInfo != NULL );
        topic = ( char * ) deserializedInfo->pPublishInfo->pTopicName;
        topicLength = deserializedInfo->pPublishInfo->topicNameLength;
        message = ( uint8_t * ) deserializedInfo->pPublishInfo->pPayload;
        messageLength = deserializedInfo->pPublishInfo->payloadLength;
        handleIncomingMQTTMessage( topic, topicLength, message, messageLength );
    }
    else
    {
        /* Handle other packets. */
        switch( packetInfo->type )
        {
            case MQTT_PACKET_TYPE_PUBACK:
                printf( "PUBACK received with packet id: %u\n",
                        ( unsigned int ) deserializedInfo->packetIdentifier );
                break;

            case MQTT_PACKET_TYPE_SUBACK:
                printf( "SUBACK received with packet id: %u\n",
                        ( unsigned int ) deserializedInfo->packetIdentifier );
                break;

            case MQTT_PACKET_TYPE_UNSUBACK:
                printf( "UNSUBACK received with packet id: %u\n",
                        ( unsigned int ) deserializedInfo->packetIdentifier );
                break;
            default:
                printf( "Error: Unknown packet type received:(%02x).\n",
                        packetInfo->type );
        }
    }
}

static void handleIncomingMQTTMessage( char * topic,
                                       size_t topicLength,
                                       uint8_t * message,
                                       size_t messageLength )

{
    
    bool messageHandled = false;
    
    if( strcmp("Hello",topic) == 0 )
    {
        printf( "Message received on topic Hello. \n    Message: %s\r\n",
                message);
        messageHandled = true;
    }
    else
    {
        messageHandled = otaDemo_handleIncomingMQTTMessage( topic,
                                                             topicLength,
                                                             message,
                                                             messageLength );
    }
    if( !messageHandled )
    {
        printf( "Unhandled incoming PUBLISH received on topic, message: "
                "%.*s\n%.*s\n",
                ( unsigned int ) topicLength,
                topic,
                ( unsigned int ) messageLength,
                ( char * ) message );
    }
}

static uint32_t getTimeMs( void )
{
    return ( uint32_t ) xTaskGetTickCount() * 1000 / configTICK_RATE_HZ;
}