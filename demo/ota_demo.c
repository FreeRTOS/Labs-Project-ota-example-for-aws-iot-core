/*
 * Copyright Amazon.com, Inc. and its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: MIT
 *
 * Licensed under the MIT License. See the LICENSE accompanying this file
 * for the specific language governing permissions and limitations under
 * the License.
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "mqtt_wrapper/mqtt_wrapper.h"
#include "ota_demo.h"

#include "ota_job_handler.h"
#include "ota_job_processor.h"

#include "MQTTFileDownloader.h"

#define CONFIG_BLOCK_SIZE    256U
#define CONFIG_MAX_FILE_SIZE 65536U
#define NUM_OF_BLOCKS_REQUESTED 1U
#define MAX_THING_NAME_SIZE 128U

uint32_t numOfBlocksRemaining = 0;
uint32_t currentBlockOffset = 0;
uint8_t currentFileId = 0;
uint32_t totalBytesReceived = 0;

#define MAX_JOB_ID_LENGTH    64U

bool otaDemo_handleJobsStartNextAccepted( const char * jobId,
                                          const size_t jobIdLength,
                                          const char * jobDoc,
                                          const size_t jobDocLength );

static uint8_t downloadedData[ CONFIG_MAX_FILE_SIZE ] = { 0 };
char globalJobId[ MAX_JOB_ID_LENGTH ] = { 0 };

void otaDemo_start( void )
{
    if( isMqttConnected() )
    {
        jobs_startNextPendingJob();
    }
}

/* Implemented for use by the MQTT library */
bool otaDemo_handleIncomingMQTTMessage( char * topic,
                                        size_t topicLength,
                                        uint8_t * message,
                                        size_t messageLength )
{
    bool handled = coreJobsMQTTAPI_handleIncomingMQTTMessage(
        &otaDemo_handleJobsStartNextAccepted,
        topic,
        topicLength,
        message,
        messageLength );

    handled = handled || mqttStreams_handleIncomingMessage( topic,
                                                            topicLength,
                                                            message,
                                                            messageLength );
    if( !handled )
    {
        printf( "Unrecognized incoming MQTT message received on topic: "
                "%.*s\nMessage: %.*s\n",
                ( unsigned int ) topicLength,
                topic,
                ( unsigned int ) messageLength,
                ( char * ) message );
    }
    return handled;
}

/* TODO: Implement for the Jobs library */
bool otaDemo_handleJobsStartNextAccepted( const char * jobId,
                                          const size_t jobIdLength,
                                          const char * jobDoc,
                                          const size_t jobDocLength )
{
    bool handled = false;
    if( globalJobId[ 0 ] == 0 )
    {
        strncpy( globalJobId, jobId, jobIdLength );
        handled = handleJobDoc( jobId, jobIdLength, jobDoc, jobDocLength );
    }
    return handled;
}

/* AFR OTA library callback */
void applicationSuppliedFunction_processAfrOtaDocument(
    AfrOtaJobDocumentFields_t * params )
{
    char thingName[MAX_THING_NAME_SIZE + 1];
    memset(thingName, '\0', MAX_THING_NAME_SIZE + 1);
    getThingName(thingName);

    numOfBlocksRemaining = params->fileSize/CONFIG_BLOCK_SIZE;
    numOfBlocksRemaining += (params->fileSize % CONFIG_BLOCK_SIZE > 0) ? 1 : 0;
    currentFileId = params->fileId;
    currentBlockOffset = 0;
    totalBytesReceived = 0;
    /* Initalize the File downloader */
    ucMqttFileDownloaderInit(params->imageRef, thingName, DATA_TYPE_CBOR);

    /* Request the first block */
    ucRequestDataBlock(currentFileId, CONFIG_BLOCK_SIZE, currentBlockOffset, NUM_OF_BLOCKS_REQUESTED);
}

/* Implemented for the MQTT Streams library */
void otaDemo_handleMqttStreamsBlockArrived( MqttFileDownloaderDataBlockInfo_t *dataBlock )
{
    assert((totalBytesReceived + dataBlock->payloadLength) < CONFIG_MAX_FILE_SIZE);
        
    memcpy( downloadedData + totalBytesReceived,
            dataBlock->payload,
            dataBlock->payloadLength );

    totalBytesReceived += dataBlock->payloadLength;
    numOfBlocksRemaining--;

    if( numOfBlocksRemaining == 0 )
    {
        printf("Downloaded Data %s \n", (char *) downloadedData);
        otaDemo_finishDownload();
    }
    else
    {
        currentBlockOffset++;
        ucRequestDataBlock(currentFileId, CONFIG_BLOCK_SIZE, currentBlockOffset, NUM_OF_BLOCKS_REQUESTED);
    }
}

void otaDemo_finishDownload()
{
    /* TODO: Do something with the completed download */
    /* Start the bootloader */
    jobs_reportJobStatusComplete();
}
