

/*
 * Demo for showing use of the MQTT V5 API.
 *
*/

/* Standard includes. */
#include <string.h>
#include <stdio.h>

/* Kernel includes. */
#include "FreeRTOS.h"
#include "task.h"

/* Demo Specific configs. */
#include "demo_config.h"

/* MQTT library includes. */
#include "core_mqtt.h"

/* Exponential backoff retry include. */
#include "backoff_algorithm.h"

/* Transport interface implementation include header for plaintext connection. */
#include "transport_plaintext.h"

/*-----------------------------------------------------------*/

/* Compile time error for undefined configs. */
#ifndef democonfigMQTT_BROKER_ENDPOINT
    #error "Define the config democonfigMQTT_BROKER_ENDPOINT by following the instructions in file demo_config.h."
#endif
/*-----------------------------------------------------------*/

/* Default values for configs. */
#ifndef democonfigCLIENT_IDENTIFIER

/**
 * @brief The MQTT client identifier used in this example.  Each client identifier
 * must be unique so edit as required to ensure no two clients connecting to the
 * same broker use the same client identifier.
 *
 * @note Appending __TIME__ to the client id string will help to create a unique
 * client id every time an application binary is built. Only a single instance of
 * this application's compiled binary may be used at a time, since the client ID
 * will always be the same.
 */
    #define democonfigCLIENT_IDENTIFIER    "testClient"__TIME__
#endif

#ifndef democonfigMQTT_BROKER_PORT

/**
 * @brief The port to use for the demo.
 */
    #define democonfigMQTT_BROKER_PORT    ( 8883 )
#endif

/*-----------------------------------------------------------*/

/**
 * @brief The maximum number of retries for network operation with server.
 */
#define mqttexampleRETRY_MAX_ATTEMPTS                     ( 5U )

/**
 * @brief The maximum back-off delay (in milliseconds) for retrying failed operation
 *  with server.
 */
#define mqttexampleRETRY_MAX_BACKOFF_DELAY_MS             ( 5000U )

/**
 * @brief The base back-off delay (in milliseconds) to use for network operation retry
 * attempts.
 */
#define mqttexampleRETRY_BACKOFF_BASE_MS                  ( 500U )

/**
 * @brief Timeout for receiving CONNACK packet in milliseconds.
 */
#define mqttexampleCONNACK_RECV_TIMEOUT_MS                ( 1000U )

/**
 * @brief The prefix to the topic(s) subscribe(d) to and publish(ed) to in the example.
 *
 * The topic name starts with the client identifier to ensure that each demo
 * interacts with a unique topic name.
 */
#define mqttexampleTOPIC_PREFIX                           democonfigCLIENT_IDENTIFIER "/example/topic"

/**
 * @brief The number of topic filters to subscribe.
 */
#define mqttexampleTOPIC_COUNT                            ( 3 )

/**
 * @brief The size of the buffer for each topic string.
 */
#define mqttexampleTOPIC_BUFFER_SIZE                      ( 100U )

/**
 * @brief The MQTT message published in this example.
 */
#define mqttexampleMESSAGE                                "Hello World!"

/**
 * @brief Time in ticks to wait between each cycle of the demo implemented
 * by prvMQTTDemoTask().
 */
#define mqttexampleDELAY_BETWEEN_DEMO_ITERATIONS_TICKS    ( pdMS_TO_TICKS( 5000U ) )

/**
 * @brief Timeout for MQTT_ProcessLoop in milliseconds.
 * Refer to FreeRTOS-Plus/Demo/coreMQTT_Windows_Simulator/readme.txt for more details.
 */
#define mqttexamplePROCESS_LOOP_TIMEOUT_MS                ( 2000U )

/**
 * @brief The keep-alive timeout period reported to the broker while establishing
 * an MQTT connection.
 *
 * It is the responsibility of the client to ensure that the interval between
 * control packets being sent does not exceed this keep-alive value. In the
 * absence of sending any other control packets, the client MUST send a
 * PINGREQ packet.
 */
#define mqttexampleKEEP_ALIVE_TIMEOUT_SECONDS             ( 60U )

/**
 * @brief Delay (in ticks) between consecutive cycles of MQTT publish operations in a
 * demo iteration.
 *
 * Note that the process loop also has a timeout, so the total time between
 * publishes is the sum of the two delays.
 */
#define mqttexampleDELAY_BETWEEN_PUBLISHES_TICKS          ( pdMS_TO_TICKS( 2000U ) )

/**
 * @brief Transport timeout in milliseconds for transport send and receive.
 */
#define mqttexampleTRANSPORT_SEND_RECV_TIMEOUT_MS         ( 200U )

/**
 * @brief The length of the outgoing publish records array used by the coreMQTT
 * library to track QoS > 0 packet ACKS for outgoing publishes.
 * Number of publishes = ulMaxPublishCount * mqttexampleTOPIC_COUNT
 * Update in ulMaxPublishCount needs updating mqttexampleOUTGOING_PUBLISH_RECORD_LEN.
 */
#define mqttexampleOUTGOING_PUBLISH_RECORD_LEN            ( 15U )

/**
 * @brief The length of the incoming publish records array used by the coreMQTT
 * library to track QoS > 0 packet ACKS for incoming publishes.
 * Number of publishes = ulMaxPublishCount * mqttexampleTOPIC_COUNT
 * Update in ulMaxPublishCount needs updating mqttexampleINCOMING_PUBLISH_RECORD_LEN.
 */
#define mqttexampleINCOMING_PUBLISH_RECORD_LEN            ( 15U )

/**
 * @brief Milliseconds per second.
 */
#define MILLISECONDS_PER_SECOND                           ( 1000U )

/**
 * @brief Milliseconds per FreeRTOS tick.
 */
#define MILLISECONDS_PER_TICK                             ( MILLISECONDS_PER_SECOND / configTICK_RATE_HZ )

/*-----------------------------------------------------------*/

/**
 * @brief Each compilation unit that consumes the NetworkContext must define it.
 * It should contain a single pointer to the type of your desired transport.
 * When using multiple transports in the same compilation unit, define this pointer as void *.
 *
 * @note Transport stacks are defined in FreeRTOS-Plus/Source/Application-Protocols/network_transport.
 */
struct NetworkContext
{
    PlaintextTransportParams_t * pParams;
};

/*-----------------------------------------------------------*/
/**
 * @brief The task used to demonstrate the MQTT API.
 *
 * @param[in] pvParameters Parameters as passed at the time of task creation. Not
 * used in this example.
 */
static void prvMQTTDemoTask( void * pvParameters );

/**
 * @brief Connect to MQTT broker with reconnection retries.
 *
 * If connection fails, retry is attempted after a timeout.
 * Timeout value will exponentially increase until the maximum
 * timeout value is reached or the number of attempts are exhausted.
 *
 * @param[out] pxNetworkContext The output parameter to return the created network context.
 *
 * @return The status of the final connection attempt.
 */
static PlaintextTransportStatus_t prvConnectToServerWithBackoffRetries( NetworkContext_t * pxNetworkContext );

/**
 * @brief Sends an MQTT Connect packet over the already connected TLS over TCP connection.
 *
 * @param[in, out] pxMQTTContext MQTT context pointer.
 * @param[in] xNetworkContext network context.
 */
static void prvCreateMQTTConnectionWithBroker( MQTTContext_t * pxMQTTContext,
                                               NetworkContext_t * pxNetworkContext,
    MQTTConnectProperties_t *pxProperties);



/**
 * @brief Publishes a message mqttexampleMESSAGE on mqttexampleTOPIC topic.
 *
 * @param[in] pxMQTTContext MQTT context pointer.
 */
static void prvMQTTPublishToTopics( MQTTContext_t * pxMQTTContext );



/**
 * @brief The timer query function provided to the MQTT context.
 *
 * @return Time in milliseconds.
 */
static uint32_t prvGetTimeMs( void );




/**
 * @brief The application callback function for getting the incoming publishes,
 * incoming acks, and ping responses reported from the MQTT library.
 *
 * @param[in] pxMQTTContext MQTT context pointer.
 * @param[in] pxPacketInfo Packet Info pointer for the incoming packet.
 * @param[in] pxDeserializedInfo Deserialized information from the incoming packet.
 */
static void prvEventCallback( MQTTContext_t * pxMQTTContext,
                              MQTTPacketInfo_t * pxPacketInfo,
                              MQTTDeserializedInfo_t * pxDeserializedInfo );

/**
 * @brief Call #MQTT_ProcessLoop in a loop for the duration of a timeout or
 * #MQTT_ProcessLoop returns a failure.
 *
 * @param[in] pMqttContext MQTT context pointer.
 * @param[in] ulTimeoutMs Duration to call #MQTT_ProcessLoop for.
 *
 * @return Returns the return value of the last call to #MQTT_ProcessLoop.
 */
static MQTTStatus_t prvProcessLoopWithTimeout( MQTTContext_t * pMqttContext,
                                               uint32_t ulTimeoutMs );

/**
 * @brief Initialize the topic filter string and SUBACK buffers.
 */
static void prvInitializeTopicBuffers( void );

/**
 * @brief Process a response or ack to an MQTT request (PING, PUBLISH,
 * SUBSCRIBE or UNSUBSCRIBE). This function processes PINGRESP, PUBACK,
 * PUBREC, PUBREL, PUBCOMP, SUBACK, and UNSUBACK.
 *
 * @param[in] pxIncomingPacket is a pointer to structure containing deserialized
 * MQTT response.
 * @param[in] usPacketId is the packet identifier from the ack received.
 */
static void prvMQTTProcessResponse(MQTTPacketInfo_t* pxIncomingPacket,
    uint16_t usPacketId);


/*-----------------------------------------------------------*/

static uint8_t ucSharedBuffer[ democonfigNETWORK_BUFFER_SIZE ];

/**
 * @brief Global entry time into the application to use as a reference timestamp
 * in the #prvGetTimeMs function. #prvGetTimeMs will always return the difference
 * between the current time and the global entry time. This will reduce the chances
 * of overflow for the 32 bit unsigned integer used for holding the timestamp.
 */
static uint32_t ulGlobalEntryTimeMs;

/**
 * @brief Packet Identifier generated when Publish request was sent to the broker;
 * it is used to match received Publish ACK to the transmitted Publish packet.
 */
static uint16_t usPublishPacketIdentifier;


/**
 * @brief A pair containing a topic filter and its SUBACK status.
 */
typedef struct topicFilterContext
{
    uint8_t pcTopicFilter[ mqttexampleTOPIC_BUFFER_SIZE ];
    MQTTSubAckStatus_t xSubAckStatus;
} topicFilterContext_t;

/**
 * @brief An array containing the context of a SUBACK; the SUBACK status
 * of a filter is updated when the event callback processes a SUBACK.
 */
static topicFilterContext_t xTopicFilterContext[ mqttexampleTOPIC_COUNT ];


/** @brief Static buffer used to hold MQTT messages being sent and received. */
static MQTTFixedBuffer_t xBuffer =
{
    ucSharedBuffer,
    democonfigNETWORK_BUFFER_SIZE
};

/**
 * @brief Array to track the outgoing publish records for outgoing publishes
 * with QoS > 0.
 *
 * This is passed into #MQTT_InitStatefulQoS to allow for QoS > 0.
 *
 */
static MQTTPubAckInfo_t pOutgoingPublishRecords[ mqttexampleOUTGOING_PUBLISH_RECORD_LEN ];

/**
 * @brief Array to track the incoming publish records for incoming publishes
 * with QoS > 0.
 *
 * This is passed into #MQTT_InitStatefulQoS to allow for QoS > 0.
 *
 */
static MQTTPubAckInfo_t pIncomingPublishRecords[ mqttexampleINCOMING_PUBLISH_RECORD_LEN ];

/*-----------------------------------------------------------*/

/*
 * @brief Create the task that demonstrates the MQTT API Demo over a
 * server-authenticated network connection with MQTT broker.
 */
void vStartSimpleMQTTDemo( void )
{
    /* This example uses a single application task, which in turn is used to
     * connect, subscribe, publish, unsubscribe, and disconnect from the MQTT
     * broker.
     *
     * Also see https://www.freertos.org/mqtt/mqtt-agent-demo.html? for an
     * alternative run time model whereby coreMQTT runs in an autonomous
     * background agent task.  Executing the MQTT protocol in an agent task
     * removes the need for the application writer to explicitly manage any MQTT
     * state or call the MQTT_ProcessLoop() API function. Using an agent task
     * also enables multiple application tasks to more easily share a single
     * MQTT connection. */
    xTaskCreate( prvMQTTDemoTask,          /* Function that implements the task. */
                 "DemoTask",               /* Text name for the task - only used for debugging. */
                 democonfigDEMO_STACKSIZE, /* Size of stack (in words, not bytes) to allocate for the task. */
                 NULL,                     /* Task parameter - not used in this case. */
                 tskIDLE_PRIORITY,         /* Task priority, must be between 0 and configMAX_PRIORITIES - 1. */
                 NULL );                   /* Used to pass out a handle to the created task - not used in this case. */
}
/*-----------------------------------------------------------*/

/*
 * @brief The Example shown below uses MQTT APIs to create MQTT messages and
 * send them over the server-authenticated network connection established with the
 * MQTT broker. This example is single-threaded and uses statically allocated
 * memory. It uses QoS2 for sending and receiving messages from the broker.
 *
 * This MQTT client subscribes to the topic as specified in mqttexampleTOPIC at the
 * top of this file by sending a subscribe packet and waiting for a subscribe
 * acknowledgment (SUBACK) from the broker. The client will then publish to the
 * same topic it subscribed to, therefore expecting that all outgoing messages will be
 * sent back from the broker.
 */
static void prvMQTTDemoTask( void * pvParameters )
{
    uint32_t ulPublishCount = 0U, ulTopicCount = 0U;
    const uint32_t ulMaxPublishCount = 5UL;
    NetworkContext_t xNetworkContext = { 0 };
    PlaintextTransportParams_t xPlaintextTransportParams = { 0 };
    MQTTContext_t xMQTTContext = { 0 };
    MQTTStatus_t xMQTTStatus;
    PlaintextTransportStatus_t xNetworkStatus;
    MQTTConnectProperties_t xProperties;
    MQTTAckInfo_t disconnect = { 0 };

    /* Remove compiler warnings about unused parameters. */
    ( void ) pvParameters;

    /* Set the pParams member of the network context with desired transport. */
    xNetworkContext.pParams = &xPlaintextTransportParams;

    /* Set the entry time of the demo application. This entry time will be used
     * to calculate relative time elapsed in the execution of the demo application,
     * by the timer utility function that is provided to the MQTT library.
     */
    ulGlobalEntryTimeMs = prvGetTimeMs();


        LogInfo( ( "---------STARTING DEMO---------\r\n" ) );

        /**************************** Initialize. *****************************/

        prvInitializeTopicBuffers();

        /****************************** Connect. ******************************/

        /* Wait for Networking */
        if( xPlatformIsNetworkUp() == pdFALSE )
        {
            LogInfo( ( "Waiting for the network link up event..." ) );

            while( xPlatformIsNetworkUp() == pdFALSE )
            {
                vTaskDelay( pdMS_TO_TICKS( 1000U ) );
            }
        }

        /* Attempt to establish a TLS connection with the MQTT broker. This example
         * connects to the MQTT broker specified in democonfigMQTT_BROKER_ENDPOINT, using
         * the port number specified in democonfigMQTT_BROKER_PORT (these macros are defined
         * in file demo_config.h). If the connection fails, attempt to re-connect after a timeout.
         * The timeout value will be exponentially increased until either the maximum timeout value
         * is reached, or the maximum number of attempts are exhausted. The function returns a failure status
         * if the TCP connection cannot be established with the broker after a configured number
         * of attempts. */
        xNetworkStatus = prvConnectToServerWithBackoffRetries( &xNetworkContext );
        configASSERT( xNetworkStatus == PLAINTEXT_TRANSPORT_SUCCESS );

        /* Send an MQTT CONNECT packet over the established TLS connection,
         * and wait for the connection acknowledgment (CONNACK) packet. */
        LogInfo( ( "Creating an MQTT connection to %s.\r\n", democonfigMQTT_BROKER_ENDPOINT ) );
        prvCreateMQTTConnectionWithBroker( &xMQTTContext, &xNetworkContext, &xProperties);


        /**************************** Publish and Keep-Alive Loop. ******************************/

        /* Publish messages with QoS2, and send and process keep-alive messages. */
            prvMQTTPublishToTopics(&xMQTTContext);
            LogInfo( ( "Attempt to receive publish acks from broker.\r\n" ) );
            xMQTTStatus = prvProcessLoopWithTimeout( &xMQTTContext, mqttexamplePROCESS_LOOP_TIMEOUT_MS );

            MQTTUserProperties_t userProperty;
            memset(&userProperty, 0x0, sizeof(userProperty));
            userProperty.count = 1;
            userProperty.userProperty[0].pKey = "Disconnect";
            userProperty.userProperty[0].pValue = "Disconnect";
            userProperty.userProperty[0].keyLength = 10;
            userProperty.userProperty[0].valueLength = 10;
            disconnect.pUserProperty = &userProperty;
            disconnect.reasonStringLength = 4;
            disconnect.pReasonString = "test";
            xMQTTStatus = MQTTV5_Disconnect(&xMQTTContext, &disconnect, 0);
            configASSERT(xMQTTStatus == MQTTSuccess);


        /* Close the network connection.  */
        Plaintext_FreeRTOS_Disconnect( &xNetworkContext );

        /* Wait for some time between two iterations to ensure that we do not
         * bombard the broker. */
        LogInfo( ( "prvMQTTDemoTask() completed an iteration successfully. Total free heap is %u.\r\n", xPortGetFreeHeapSize() ) );
        LogInfo( ( "Demo completed successfully.\r\n" ) );
        LogInfo( ( "-------DEMO FINISHED-------\r\n" ) );
        vTaskDelay( mqttexampleDELAY_BETWEEN_DEMO_ITERATIONS_TICKS );
    
}
/*-----------------------------------------------------------*/

static PlaintextTransportStatus_t prvConnectToServerWithBackoffRetries( NetworkContext_t * pxNetworkContext )
{
    PlaintextTransportStatus_t xNetworkStatus;
    BackoffAlgorithmStatus_t xBackoffAlgStatus = BackoffAlgorithmSuccess;
    BackoffAlgorithmContext_t xReconnectParams;
    uint16_t usNextRetryBackOff = 0U;

    /* Initialize reconnect attempts and interval.*/
    BackoffAlgorithm_InitializeParams( &xReconnectParams,
                                       mqttexampleRETRY_BACKOFF_BASE_MS,
                                       mqttexampleRETRY_MAX_BACKOFF_DELAY_MS,
                                       mqttexampleRETRY_MAX_ATTEMPTS );

    /* Attempt to connect to MQTT broker. If connection fails, retry after
     * a timeout. Timeout value will exponentially increase till maximum
     * attempts are reached.
     */
    do
    {
        /* Establish a TCP connection with the MQTT broker. This example connects to
         * the MQTT broker as specified in democonfigMQTT_BROKER_ENDPOINT and
         * democonfigMQTT_BROKER_PORT at the top of this file. */
        LogInfo( ( "Create a TCP connection to %s:%d.",
                   democonfigMQTT_BROKER_ENDPOINT,
                   democonfigMQTT_BROKER_PORT ) );
        xNetworkStatus = Plaintext_FreeRTOS_Connect( pxNetworkContext,
                                                     democonfigMQTT_BROKER_ENDPOINT,
                                                     democonfigMQTT_BROKER_PORT,
                                                     mqttexampleTRANSPORT_SEND_RECV_TIMEOUT_MS,
                                                     mqttexampleTRANSPORT_SEND_RECV_TIMEOUT_MS );

        if( xNetworkStatus != PLAINTEXT_TRANSPORT_SUCCESS )
        {
            /* Generate a random number and calculate backoff value (in milliseconds) for
             * the next connection retry.
             * Note: It is recommended to seed the random number generator with a device-specific
             * entropy source so that possibility of multiple devices retrying failed network operations
             * at similar intervals can be avoided. */
            xBackoffAlgStatus = BackoffAlgorithm_GetNextBackoff( &xReconnectParams, uxRand(), &usNextRetryBackOff );

            if( xBackoffAlgStatus == BackoffAlgorithmRetriesExhausted )
            {
                LogError( ( "Connection to the broker failed, all attempts exhausted." ) );
            }
            else if( xBackoffAlgStatus == BackoffAlgorithmSuccess )
            {
                LogWarn( ( "Connection to the broker failed. "
                           "Retrying connection with backoff and jitter." ) );
                vTaskDelay( pdMS_TO_TICKS( usNextRetryBackOff ) );
            }
        }
    } while( ( xNetworkStatus != PLAINTEXT_TRANSPORT_SUCCESS ) && ( xBackoffAlgStatus == BackoffAlgorithmSuccess ) );

    return xNetworkStatus;
}
/*-----------------------------------------------------------*/

static void prvCreateMQTTConnectionWithBroker( MQTTContext_t * pxMQTTContext,
                                               NetworkContext_t * pxNetworkContext ,
                                               MQTTConnectProperties_t *pxProperties)
{
    MQTTStatus_t xResult;
    MQTTConnectInfo_t xConnectInfo;
    MQTTUserProperties_t userProperty;
    MQTTAckInfo_t disconnect;
    PlaintextTransportStatus_t xNetworkStatus;
    MQTTPublishInfo_t willInfo;
    MQTTAuthInfo_t auth;
    bool xSessionPresent;
    TransportInterface_t xTransport;

    /***
     * For readability, error handling in this function is restricted to the use of
     * asserts().
     ***/

    /* Fill in Transport Interface send and receive function pointers. */
    xTransport.pNetworkContext = pxNetworkContext;
    xTransport.send = Plaintext_FreeRTOS_send;
    xTransport.recv = Plaintext_FreeRTOS_recv;
    xTransport.writev = NULL;

    /* Initialize MQTT library. */
    xResult = MQTT_Init( pxMQTTContext, &xTransport, prvGetTimeMs, prvEventCallback, &xBuffer );
    configASSERT( xResult == MQTTSuccess );
    xResult = MQTT_InitStatefulQoS( pxMQTTContext,
                                    pOutgoingPublishRecords,
                                    mqttexampleOUTGOING_PUBLISH_RECORD_LEN,
                                    pIncomingPublishRecords,
                                    mqttexampleINCOMING_PUBLISH_RECORD_LEN );
    configASSERT( xResult == MQTTSuccess );

    /* Some fields are not used in this demo so start with everything at 0. */
    ( void ) memset( ( void * ) &xConnectInfo, 0x00, sizeof( xConnectInfo ) );
    ( void ) memset( ( void * ) pxProperties, 0x00, sizeof(*pxProperties) );
    (void)memset((void*)&disconnect, 0x00, sizeof(disconnect));
    (void)memset((void*)&willInfo, 0x00, sizeof(willInfo));
    (void)memset((void*)&auth, 0x00, sizeof(auth));
    xResult = MQTTV5_InitConnect(pxProperties);
    configASSERT(xResult == MQTTSuccess);
    pxProperties->pIncomingUserProperty = &userProperty;
    pxMQTTContext->pConnectProperties = pxProperties;

    /* The client identifier is used to uniquely identify this MQTT client to
     * the MQTT broker. In a production device the identifier can be something
     * unique, such as a device serial number. */
    xConnectInfo.pClientIdentifier = democonfigCLIENT_IDENTIFIER;
    xConnectInfo.clientIdentifierLength = ( uint16_t ) strlen( democonfigCLIENT_IDENTIFIER );

    /* Set MQTT keep-alive period. If the application does not send packets at an interval less than
     * the keep-alive period, the MQTT library will send PINGREQ packets. */
    xConnectInfo.keepAliveSeconds = mqttexampleKEEP_ALIVE_TIMEOUT_SECONDS;

    LogInfo(("Create a bad connection with the broker"));

    /*Bad Authentication.*/
    auth.authDataLength = 4;
    auth.authMethodLength = 4;
    auth.pAuthData = "test";
    auth.pAuthMethod = "test";
    pxProperties->pOutgoingAuth = &auth;
    pxProperties->pIncomingAuth = &auth;
    xResult = MQTT_Connect(pxMQTTContext, &xConnectInfo, NULL, mqttexampleCONNACK_RECV_TIMEOUT_MS,
        &xSessionPresent);


    /*LWT verification with user properties.*/
    /*LWT with will delay.*/
    LogInfo(("Create a good connection with the broker and disconnect without sending the disconnect packet to validate will delay"));
    xConnectInfo.cleanSession = true;
    xConnectInfo.clientIdentifierLength = 5;
    xConnectInfo.pClientIdentifier = "abcde";
    xNetworkStatus = prvConnectToServerWithBackoffRetries(pxNetworkContext);
    configASSERT(xNetworkStatus == PLAINTEXT_TRANSPORT_SUCCESS);
    willInfo.pTopicName = "TestWill1234";
    willInfo.topicNameLength = 12;
    willInfo.pUserProperty = &userProperty;
    willInfo.payloadLength = 15;
    willInfo.pPayload = "TestWillPayload";
    willInfo.willDelay = 30;
    userProperty.count = 1;
    userProperty.userProperty[0].pKey = "Key1";
    userProperty.userProperty[0].pValue = "Value1";
    userProperty.userProperty[0].keyLength= 4;
    userProperty.userProperty[0].valueLength = 6;
    pxProperties->pOutgoingAuth = NULL;
    xResult = MQTT_Connect(pxMQTTContext,
        &xConnectInfo,
        &willInfo,
        mqttexampleCONNACK_RECV_TIMEOUT_MS,
        &xSessionPresent);
    Plaintext_FreeRTOS_Disconnect(pxNetworkContext);


    /* Send MQTT CONNECT packet to broker. LWT is not used in this demo, so it
     * is passed as NULL. */
    LogInfo(("Create  a good connection with the broker"));
    xConnectInfo.pClientIdentifier = democonfigCLIENT_IDENTIFIER;
    xConnectInfo.clientIdentifierLength = (uint16_t)strlen(democonfigCLIENT_IDENTIFIER);
    pxProperties->sessionExpiry = 20;
    pxProperties->maxPacketSize = 200;
    pxProperties->requestResponseInfo = 1;
    pxProperties->receiveMax = 20;
    pxProperties->topicAliasMax = 20;
    xNetworkStatus = prvConnectToServerWithBackoffRetries(pxNetworkContext);
    configASSERT(xNetworkStatus == PLAINTEXT_TRANSPORT_SUCCESS);
    xResult = MQTT_Connect( pxMQTTContext,
                            &xConnectInfo,
                            NULL,
                            mqttexampleCONNACK_RECV_TIMEOUT_MS,
                            &xSessionPresent );
    configASSERT( xResult == MQTTSuccess );

    /* Successfully established and MQTT connection with the broker. */
    LogInfo( ( "An MQTT connection is established with %s.", democonfigMQTT_BROKER_ENDPOINT ) );
}
/*-----------------------------------------------------------*/

static void prvMQTTPublishToTopics( MQTTContext_t * pxMQTTContext )
{
    MQTTStatus_t xResult;
    MQTTPublishInfo_t xMQTTPublishInfo;
    /***
     * For readability, error handling in this function is restricted to the use of
     * asserts().
     ***/
        /* Some fields are not used by this demo so start with everything at 0. */
        ( void ) memset( ( void * ) &xMQTTPublishInfo, 0x00, sizeof( xMQTTPublishInfo ) );
        /* This demo uses QoS0 */
        MQTTUserProperties_t userProperty;
        userProperty.count = 1;
        userProperty.userProperty[0].pKey = "Key1";
        userProperty.userProperty[0].pValue = "Value1";
        userProperty.userProperty[0].keyLength = 4;
        userProperty.userProperty[0].valueLength = 6;
        xMQTTPublishInfo.topicAlias = 2U;
        xMQTTPublishInfo.qos = MQTTQoS2;
        xMQTTPublishInfo.retain = false;
        xMQTTPublishInfo.pTopicName = "TestUnique1234";
        xMQTTPublishInfo.topicNameLength = 14;
        xMQTTPublishInfo.pPayload = mqttexampleMESSAGE;
        xMQTTPublishInfo.payloadLength = strlen( mqttexampleMESSAGE );
        xMQTTPublishInfo.pUserProperty = &userProperty;

        /* Get a unique packet id. */
        usPublishPacketIdentifier = MQTT_GetPacketId( pxMQTTContext );

        LogInfo( ( "Publishing to the MQTT topic %s.\r\n", xMQTTPublishInfo.pTopicName));
        /* Send PUBLISH packet. */
        xResult = MQTT_Publish( pxMQTTContext, &xMQTTPublishInfo, usPublishPacketIdentifier );
        configASSERT( xResult == MQTTSuccess );

        /*Publish using only topic alias*/
        xMQTTPublishInfo.topicAlias = 2U;
        xMQTTPublishInfo.topicNameLength = 0U;
        xMQTTPublishInfo.pUserProperty = NULL;
        xMQTTPublishInfo.pPayload = "OnlyTopicAlias";
        xMQTTPublishInfo.payloadLength = 14;
        usPublishPacketIdentifier = MQTT_GetPacketId(pxMQTTContext);
        LogInfo( ( "Publishing to the MQTT topic only using topic alias "));
        /* Send PUBLISH packet. */
        xResult = MQTT_Publish(pxMQTTContext, &xMQTTPublishInfo, usPublishPacketIdentifier);
        configASSERT(xResult == MQTTSuccess);

        /*Publish using Qos 0*/
        xMQTTPublishInfo.qos = MQTTQoS0;
        xMQTTPublishInfo.pPayload = "UsingQos0";
        xMQTTPublishInfo.payloadLength = 9; 
        LogInfo(("Publishing Qos0  "));


        xResult = MQTT_Publish(pxMQTTContext, &xMQTTPublishInfo, usPublishPacketIdentifier);
        configASSERT(xResult == MQTTSuccess);

        /*Publish using Qos 1*/
        xMQTTPublishInfo.qos = MQTTQoS1;
        xMQTTPublishInfo.pPayload = "UsingQos1";
        xMQTTPublishInfo.payloadLength = 9;
        xMQTTPublishInfo.pCorrelationData = "test";
        xMQTTPublishInfo.correlationLength = 4;
        xMQTTPublishInfo.contentTypeLength = 4;
        xMQTTPublishInfo.msgExpiryInterval = 100;
        xMQTTPublishInfo.msgExpiryPresent = true;
        xMQTTPublishInfo.pContentType = "test";
        LogInfo(("Publishing Qos1 "));

        usPublishPacketIdentifier = MQTT_GetPacketId(pxMQTTContext);
        xResult = MQTT_Publish(pxMQTTContext, &xMQTTPublishInfo, usPublishPacketIdentifier);
        configASSERT(xResult == MQTTSuccess);
    }

/*-----------------------------------------------------------*/


static void prvMQTTProcessResponse(MQTTPacketInfo_t* pxIncomingPacket,
    uint16_t usPacketId)
{
    uint32_t ulTopicCount = 0U;

    switch (pxIncomingPacket->type)
    {
    case MQTT_PACKET_TYPE_PUBACK:
        LogInfo(("PUBACK received for packet ID %u.\r\n", usPacketId));
        break;


    case MQTT_PACKET_TYPE_PINGRESP:

        /* Nothing to be done from application as library handles
         * PINGRESP with the use of MQTT_ProcessLoop API function. */
        LogWarn(("PINGRESP should not be handled by the application "
            "callback when using MQTT_ProcessLoop.\n"));
        break;

    case MQTT_PACKET_TYPE_PUBREC:
        LogInfo(("PUBREC received for packet id %u.\n\n",
            usPacketId));
        break;

    case MQTT_PACKET_TYPE_PUBREL:

        /* Nothing to be done from application as library handles
         * PUBREL. */
        LogInfo(("PUBREL received for packet id %u.\n\n",
            usPacketId));
        break;

    case MQTT_PACKET_TYPE_PUBCOMP:

        /* Nothing to be done from application as library handles
         * PUBCOMP. */
        LogInfo(("PUBCOMP received for packet id %u.\n\n",
            usPacketId));
        break;

        /* Any other packet type is invalid. */
    default:
        LogWarn(("prvMQTTProcessResponse() called with unknown packet type:(%02X).\r\n",
            pxIncomingPacket->type));
    }
}




static void prvEventCallback( MQTTContext_t * pxMQTTContext,
                              MQTTPacketInfo_t * pxPacketInfo,
                              MQTTDeserializedInfo_t * pxDeserializedInfo )
{
    /* The MQTT context is not used in this function. */
    ( void ) pxMQTTContext;
        if (pxPacketInfo->type == MQTT_PACKET_TYPE_PUBREC)
        {
             pxDeserializedInfo->pNextAckInfo->pReasonString = "test";
             pxDeserializedInfo->pNextAckInfo->reasonStringLength = 4;
            prvMQTTProcessResponse(pxPacketInfo, pxDeserializedInfo->packetIdentifier);
        }
        else
        {
            pxDeserializedInfo->pNextAckInfo = NULL;
            prvMQTTProcessResponse(pxPacketInfo, pxDeserializedInfo->packetIdentifier);

        }
    }


/*-----------------------------------------------------------*/

static uint32_t prvGetTimeMs( void )
{
    TickType_t xTickCount = 0;
    uint32_t ulTimeMs = 0UL;

    /* Get the current tick count. */
    xTickCount = xTaskGetTickCount();

    /* Convert the ticks to milliseconds. */
    ulTimeMs = ( uint32_t ) xTickCount * MILLISECONDS_PER_TICK;

    /* Reduce ulGlobalEntryTimeMs from obtained time so as to always return the
     * elapsed time in the application. */
    ulTimeMs = ( uint32_t ) ( ulTimeMs - ulGlobalEntryTimeMs );

    return ulTimeMs;
}

/*-----------------------------------------------------------*/

static MQTTStatus_t prvProcessLoopWithTimeout( MQTTContext_t * pMqttContext,
                                               uint32_t ulTimeoutMs )
{
    uint32_t ulMqttProcessLoopTimeoutTime;
    uint32_t ulCurrentTime;

    MQTTStatus_t eMqttStatus = MQTTSuccess;

    ulCurrentTime = pMqttContext->getTime();
    ulMqttProcessLoopTimeoutTime = ulCurrentTime + ulTimeoutMs;

    /* Call MQTT_ProcessLoop multiple times a timeout happens, or
     * MQTT_ProcessLoop fails. */
    while( ( ulCurrentTime < ulMqttProcessLoopTimeoutTime ) &&
           ( eMqttStatus == MQTTSuccess || eMqttStatus == MQTTNeedMoreBytes ) )
    {
        eMqttStatus = MQTT_ProcessLoop( pMqttContext );
        ulCurrentTime = pMqttContext->getTime();
    }

    if( eMqttStatus == MQTTNeedMoreBytes )
    {
        eMqttStatus = MQTTSuccess;
    }

    return eMqttStatus;
}

/*-----------------------------------------------------------*/

static void prvInitializeTopicBuffers( void )
{
    uint32_t ulTopicCount;
    int xCharactersWritten;


    for( ulTopicCount = 0; ulTopicCount < mqttexampleTOPIC_COUNT; ulTopicCount++ )
    {
        /* Write topic strings into buffers. */
        xCharactersWritten = snprintf( xTopicFilterContext[ ulTopicCount ].pcTopicFilter,
                                       mqttexampleTOPIC_BUFFER_SIZE,
                                       "%s%d", mqttexampleTOPIC_PREFIX, ( int ) ulTopicCount );

        configASSERT( xCharactersWritten >= 0 && xCharactersWritten < mqttexampleTOPIC_BUFFER_SIZE );

        /* Assign topic string to its corresponding SUBACK code initialized as a failure. */
        xTopicFilterContext[ ulTopicCount ].xSubAckStatus = MQTTSubAckFailure;
    }
}

/*-----------------------------------------------------------*/