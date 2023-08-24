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

#include "core_jobs.h"
#include "core_json.h"
#include "mqtt_wrapper.h"

#define TOPIC_BUFFER_SIZE     256U
#define MAX_THING_NAME_LENGTH 128U
/* This accounts for the message fields and client token (128 chars) */
#define START_JOB_MSG_LENGTH  147U
/* This accounts for the message fields and expected version size (up to '999')
 */
#define UPDATE_JOB_MSG_LENGTH 48U
#define UPDATE_JOB_STATUS_MAX_LENGTH 8U

static const char * jobStatusString[ 5U ] = { "QUEUED",
                                              "IN_PROGRESS",
                                              "FAILED",
                                              "SUCCEEDED",
                                              "REJECTED" };

static const size_t jobStatusStringLengths[ 5U ] = {
    sizeof( "QUEUED" ) - 1U,
    sizeof( "IN_PROGRESS" ) - 1U,
    sizeof( "FAILED" ) - 1U,
    sizeof( "SUCCEEDED" ) - 1U,
    sizeof( "REJECTED" ) - 1U
};

static const char * jobUpdateStatusString[ 2U ] = { "accepted", "rejected" };
static const char * jobUpdateStatusStringLengths[ 2U ] = { sizeof("accepted") - 1U, sizeof("rejected") - 1U };

static size_t getStartNextPendingJobExecutionTopic( const char * thingname,
                                                    size_t thingnameLength,
                                                    char * buffer,
                                                    size_t bufferSize );
static size_t getStartNextPendingJobExecutionMsg( const char * clientToken,
                                                  size_t clientTokenLength,
                                                  char * buffer,
                                                  size_t bufferSize );
static size_t getUpdateJobExecutionTopic( char * thingname,
                                          size_t thingnameLength,
                                          char * jobId,
                                          size_t jobIdLength,
                                          char * buffer,
                                          size_t bufferSize );
static size_t getUpdateJobExecutionMsg( JobStatus_t status,
                                        char * expectedVersion,
                                        size_t expectedVersionLength,
                                        char * buffer,
                                        size_t bufferSize );
static bool isThingnameTopicMatch(const char * topic,
                                    const size_t topicLength,
                                    const char * topicSuffix,
                                    const size_t topicSuffixLength );

/* TODO: Consider creating a 'thingName topic matching' function. This could simply call that function */
bool coreJobs_isStartNextAccepted( const char * topic,
                                   const size_t topicLength )
{
    return isThingnameTopicMatch(topic, topicLength, "/jobs/start-next/accepted", strlen("/jobs/start-next/accepted"));
}

bool coreJobs_isJobUpdateStatus( const char * topic,
                                const size_t topicLength,
                                const char * jobId,
                                const size_t jobIdLength,
                                JobUpdateStatus_t expectedStatus )
{
    /* Max suffix size = max topic size - "$aws/<thingname>" prefix */
    char suffixBuffer[ TOPIC_BUFFER_SIZE - MAX_THING_NAME_LENGTH - 4U] = { 0 };
    char jobIdTerminated[ MAX_JOB_ID_LENGTH + 1 ] = { 0 };
    char updateStatusString[ UPDATE_JOB_STATUS_MAX_LENGTH + 1 ] = { 0 };

    memcpy(&jobIdTerminated, jobId, jobIdLength);
    memcpy(&updateStatusString, jobUpdateStatusString[ expectedStatus ], jobUpdateStatusStringLengths[ expectedStatus ]);

    snprintf( suffixBuffer,
              TOPIC_BUFFER_SIZE - MAX_THING_NAME_LENGTH - 4U,
              "%s%s%s%s",
              "/jobs/",
              jobIdTerminated,
              "/update/",
              updateStatusString );

    return isThingnameTopicMatch(topic, topicLength, suffixBuffer, strnlen(suffixBuffer, TOPIC_BUFFER_SIZE - MAX_THING_NAME_LENGTH - 4U));
}

size_t coreJobs_getJobId(const char * message, size_t messageLength, char ** jobId)
{
    size_t jobIdLength = 0U;
    JSONStatus_t jsonResult = JSONNotFound;
    jsonResult = JSON_Validate( message, messageLength );
    if( jsonResult == JSONSuccess )
    {
        jsonResult = JSON_Search( ( char * ) message,
                                  messageLength,
                                  "execution.jobId",
                                  sizeof( "execution.jobId" ) - 1,
                                  jobId,
                                  &jobIdLength );
    }
    return jobIdLength;
}

size_t coreJobs_getJobDocument(const char * message, size_t messageLength, char ** jobDoc)
{
    size_t jobDocLength = 0U;
    JSONStatus_t jsonResult = JSONNotFound;
    jsonResult = JSON_Validate( message, messageLength );
    if( jsonResult == JSONSuccess )
    {
        jsonResult = JSON_Search( ( char * ) message,
                                  messageLength,
                                  "execution.jobDocument",
                                  sizeof( "execution.jobDocument" ) - 1,
                                  jobDoc,
                                  &jobDocLength );
    }

    return jobDocLength;
}

bool coreJobs_startNextPendingJob( char * thingname,
                                   size_t thingnameLength,
                                   char * clientToken,
                                   size_t clientTokenLength )
{
    bool published = false;
    char topicBuffer[ TOPIC_BUFFER_SIZE + 1 ] = { 0 };
    char messageBuffer[ START_JOB_MSG_LENGTH ] = { 0 };

    size_t
        topicLength = getStartNextPendingJobExecutionTopic( thingname,
                                                            thingnameLength,
                                                            topicBuffer,
                                                            TOPIC_BUFFER_SIZE );

    size_t messageLength = getStartNextPendingJobExecutionMsg(
        clientToken,
        clientTokenLength,
        messageBuffer,
        ( START_JOB_MSG_LENGTH ) );

    if( topicLength > 0U && messageLength > 0U )
    {
        published = mqttWrapper_publish( topicBuffer,
                                         topicLength,
                                         ( uint8_t * ) messageBuffer,
                                         messageLength );
    }

    return published;
}

/* TODO - Update input parameters, there are too many currently */
bool coreJobs_updateJobStatus( char * thingname,
                               size_t thingnameLength,
                               char * jobId,
                               size_t jobIdLength,
                               JobStatus_t status,
                               char * expectedVersion,
                               size_t expectedVersionLength )
{
    bool published = false;
    char topicBuffer[ TOPIC_BUFFER_SIZE + 1 ] = { 0 };
    char messageBuffer[ UPDATE_JOB_MSG_LENGTH ] = { 0 };

    size_t topicLength = getUpdateJobExecutionTopic( thingname,
                                                     thingnameLength,
                                                     jobId,
                                                     jobIdLength,
                                                     topicBuffer,
                                                     TOPIC_BUFFER_SIZE );

    size_t messageLength = getUpdateJobExecutionMsg( status,
                                                     expectedVersion,
                                                     expectedVersionLength,
                                                     messageBuffer,
                                                     UPDATE_JOB_MSG_LENGTH );

    if( topicLength > 0 && messageLength > 0 )
    {
        published = mqttWrapper_publish( topicBuffer,
                                         topicLength,
                                         ( uint8_t * ) messageBuffer,
                                         messageLength );
    }

    return published;
}

static size_t getStartNextPendingJobExecutionTopic( const char * thingname,
                                                    size_t thingnameLength,
                                                    char * buffer,
                                                    size_t bufferSize )
{
    size_t topicLength = 0U; 

    if (thingname != NULL && thingnameLength > 0U && ( bufferSize >= 28U + thingnameLength ))
    {
        topicLength = sizeof( "$aws/things/" ) - 1;
        memcpy( buffer, "$aws/things/", sizeof( "$aws/things/" ) - 1 );
        memcpy( buffer + topicLength, thingname, thingnameLength );
        topicLength += thingnameLength;
        memcpy( buffer + topicLength,
                "/jobs/start-next",
                sizeof( "/jobs/start-next" ) - 1 );
        topicLength += sizeof( "/jobs/start-next" ) - 1;
    }

    return topicLength;
}

static size_t getStartNextPendingJobExecutionMsg( const char * clientToken,
                                                  size_t clientTokenLength,
                                                  char * buffer,
                                                  size_t bufferSize )
{
    size_t messageLength = 0U;

    if ( clientToken != NULL  && clientTokenLength > 0U && ( bufferSize >= 18U + clientTokenLength ) )
    {
        messageLength = sizeof( "{\"clientToken\":\"" ) - 1;
        memcpy( buffer,
                "{\"clientToken\":\"",
                sizeof( "{\"clientToken\":\"" ) - 1 );
        memcpy( buffer + messageLength, clientToken, clientTokenLength );
        messageLength += clientTokenLength;
        memcpy( buffer + messageLength, "\"}", sizeof( "\"}" ) - 1 );
        messageLength += sizeof( "\"}" ) - 1;
    }

    return messageLength;
}

static size_t getUpdateJobExecutionTopic( char * thingname,
                                          size_t thingnameLength,
                                          char * jobId,
                                          size_t jobIdLength,
                                          char * buffer,
                                          size_t bufferSize )
{
    size_t topicLength = 0;

    if ( thingname != NULL && jobId != NULL && thingnameLength > 0U && jobIdLength > 0U && (( bufferSize >= 25U + thingnameLength + jobIdLength )))
    {
        topicLength = sizeof( "$aws/things/" ) - 1;
        memcpy( buffer, "$aws/things/", sizeof( "$aws/things/" ) - 1 );
        memcpy( buffer + topicLength, thingname, thingnameLength );
        topicLength += thingnameLength;
        memcpy( buffer + topicLength, "/jobs/", sizeof( "/jobs/" ) - 1 );
        topicLength += sizeof( "/jobs/" ) - 1;
        memcpy( buffer + topicLength, jobId, jobIdLength );
        topicLength += jobIdLength;
        memcpy( buffer + topicLength, "/update", sizeof( "/update" ) - 1 );
        topicLength += sizeof( "/update" ) - 1;
    }

    return topicLength;
}

static size_t getUpdateJobExecutionMsg( JobStatus_t status,
                                        char * expectedVersion,
                                        size_t expectedVersionLength,
                                        char * buffer,
                                        size_t bufferSize )
{
    size_t messageLength = 0;

    if ( expectedVersion != NULL && expectedVersionLength > 0U && bufferSize >=
            34U + expectedVersionLength + jobStatusStringLengths[ status ] )
    {
        messageLength = sizeof( "{\"status\":\"" ) - 1;
        memcpy( buffer, "{\"status\":\"", sizeof( "{\"status\":\"" ) - 1 );
        memcpy( buffer + messageLength,
                jobStatusString[ status ],
                jobStatusStringLengths[ status ] );

        messageLength += jobStatusStringLengths[ status ];
        memcpy( buffer + messageLength,
                "\",\"expectedVersion\":\"",
                sizeof( "\",\"expectedVersion\":\"" ) - 1 );
        messageLength += sizeof( "\",\"expectedVersion\":\"" ) - 1;
        memcpy( buffer + messageLength, expectedVersion, expectedVersionLength );
        messageLength += expectedVersionLength;
        memcpy( buffer + messageLength, "\"}", sizeof( "\"}" ) - 1 );
        messageLength += sizeof( "\"}" ) - 1;
    }

    return messageLength;
}

static bool isThingnameTopicMatch(const char * topic,
                                    const size_t topicLength,
                                    const char * topicSuffix,
                                    const size_t topicSuffixLength )
{
    /* TODO: Inefficient - better implementation shouldn't use snprintf */
    char expectedTopicBuffer[ TOPIC_BUFFER_SIZE + 1 ] = { 0 };
    char thingName[ MAX_THING_NAME_LENGTH + 1 ] = { 0 };
    char suffixTerminated[ MAX_JOB_ID_LENGTH + 1 ] = { 0 };
    size_t thingNameLength = 0U;
    bool isMatch = true;

    if ( topic == NULL || topicLength == 0 )
    {
        isMatch = false;
    }

    if ( isMatch )
    {
        mqttWrapper_getThingName( thingName, &thingNameLength );
        memcpy(suffixTerminated, topicSuffix, topicSuffixLength);
        snprintf( expectedTopicBuffer,
              TOPIC_BUFFER_SIZE,
              "%s%s%s",
              "$aws/things/",
              thingName,
              suffixTerminated );
        isMatch = ( uint32_t ) strnlen( expectedTopicBuffer, TOPIC_BUFFER_SIZE ) ==
              topicLength;
        isMatch = isMatch && strncmp( expectedTopicBuffer, topic, topicLength ) == 0;
    }

    return isMatch;
}
