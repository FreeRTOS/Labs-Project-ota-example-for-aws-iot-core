/*
 * Copyright Amazon.com, Inc. and its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: MIT
 *
 * Licensed under the MIT License. See the LICENSE accompanying this file
 * for the specific language governing permissions and limitations under
 * the License.
 */

#include <assert.h>
#include <string.h>

#include "mqtt_wrapper.h"

static MQTTContext_t * globalCoreMqttContext = NULL;

#define MAX_THING_NAME_SIZE 128U
static char globalThingName[ MAX_THING_NAME_SIZE + 1 ];

void mqttWrapper_setCoreMqttContext( MQTTContext_t * mqttContext )
{
    globalCoreMqttContext = mqttContext;
}

MQTTContext_t * mqttWrapper_getCoreMqttContext( void )
{
    assert( globalCoreMqttContext != NULL );
    return globalCoreMqttContext;
}

void mqttWrapper_setThingName( char * thingName )
{
    strncpy( globalThingName, thingName, MAX_THING_NAME_SIZE );
}

void mqttWrapper_getThingName( char * thingNameBuffer )
{
    size_t thingNameLength = 0;
    assert( globalThingName[ 0 ] != 0 );
    thingNameLength = strlen( globalThingName );
    memcpy( thingNameBuffer, globalThingName, thingNameLength );
    thingNameBuffer[ thingNameLength ] = '\0';
}

bool mqttWrapper_connect( )
{
    MQTTConnectInfo_t connectInfo = { 0 };
    MQTTStatus_t mqttStatus = MQTTSuccess;
    bool sessionPresent = false;

    assert( globalCoreMqttContext != NULL );

    connectInfo.pClientIdentifier = globalThingName;
    connectInfo.clientIdentifierLength = strlen(globalThingName);
    connectInfo.pUserName = NULL;
    connectInfo.userNameLength = 0U;
    connectInfo.pPassword = NULL;
    connectInfo.passwordLength = 0U;
    connectInfo.keepAliveSeconds = 60U;
    connectInfo.cleanSession = true;
    mqttStatus = MQTT_Connect( globalCoreMqttContext,
                               &connectInfo,
                               NULL,
                               5000U,
                               &sessionPresent );
    return mqttStatus == MQTTSuccess;
}

bool mqttWrapper_isConnected( void )
{
    bool isConnected = false;
    assert( globalCoreMqttContext != NULL );
    isConnected = globalCoreMqttContext->connectStatus == MQTTConnected;
    return isConnected;
}

bool mqttWrapper_publish( char * topic,
                          size_t topicLength,
                          uint8_t * message,
                          size_t messageLength )
{
    bool success = false;
    assert( globalCoreMqttContext != NULL );

    success = mqttWrapper_isConnected();
    if( success )
    {
        MQTTStatus_t mqttStatus = MQTTSuccess;
        MQTTPublishInfo_t pubInfo = { 0 };
        pubInfo.qos = 0;
        pubInfo.retain = false;
        pubInfo.dup = false;
        pubInfo.pTopicName = topic;
        pubInfo.topicNameLength = topicLength;
        pubInfo.pPayload = message;
        pubInfo.payloadLength = messageLength;

        mqttStatus = MQTT_Publish( globalCoreMqttContext,
                                   &pubInfo,
                                   MQTT_GetPacketId( globalCoreMqttContext ) );
        success = mqttStatus == MQTTSuccess;
    }
    return success;
}

bool mqttWrapper_subscribe( char * topic, size_t topicLength )
{
    bool success = false;
    assert( globalCoreMqttContext != NULL );

    success = mqttWrapper_isConnected();
    if( success )
    {
        MQTTStatus_t mqttStatus = MQTTSuccess;
        MQTTSubscribeInfo_t subscribeInfo = { 0 };
        subscribeInfo.qos = 0;
        subscribeInfo.pTopicFilter = topic;
        subscribeInfo.topicFilterLength = topicLength;

        mqttStatus = MQTT_Subscribe( globalCoreMqttContext,
                                     &subscribeInfo,
                                     1,
                                     MQTT_GetPacketId(
                                         globalCoreMqttContext ) );
        success = mqttStatus == MQTTSuccess;
    }
    return success;
}

bool mqttWrapper_unsubscribe( char * topic, size_t topicLength )
{
    bool success = false;

    success = mqttWrapper_isConnected();

    if ( success )
    {
        MQTTStatus_t mqttStatus = MQTTSuccess;
        MQTTSubscribeInfo_t unsubscribeList = { 0 };
        uint16_t packetId;
        MQTTContext_t * context;
        unsubscribeList.qos = 0;
        unsubscribeList.pTopicFilter = topic;
        unsubscribeList.topicFilterLength = topicLength;

        context = mqttWrapper_getCoreMqttContext();
        packetId = MQTT_GetPacketId(context);

        mqttStatus = MQTT_Unsubscribe(context,
                                      &unsubscribeList,
                                      1,
                                      packetId);
        success = mqttStatus == MQTTSuccess;
    }
    return success;
}
