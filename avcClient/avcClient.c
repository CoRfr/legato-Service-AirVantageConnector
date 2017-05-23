/**
 * @file avcClient.c
 *
 * client of the LWM2M stack
 *
 * Copyright (C) Sierra Wireless Inc.
 *
 */

/* include files */
#include <stdbool.h>
#include <stdint.h>
#include <lwm2mcore/lwm2mcore.h>
#include <lwm2mcore/timer.h>
#include <lwm2mcore/security.h>

#include "legato.h"
#include "interfaces.h"

//--------------------------------------------------------------------------------------------------
/**
 * Static instance reference for LWM2MCore
 */
//--------------------------------------------------------------------------------------------------
static lwm2mcore_Ref_t Lwm2mInstanceRef = NULL;

//--------------------------------------------------------------------------------------------------
/**
 * Static data connection state for agent
 */
//--------------------------------------------------------------------------------------------------
static bool DataConnected = false;

//--------------------------------------------------------------------------------------------------
/**
 * Static data reference
 */
//--------------------------------------------------------------------------------------------------
static le_data_RequestObjRef_t DataRef = NULL;

//--------------------------------------------------------------------------------------------------
/**
 * Static data connection handler
 */
//--------------------------------------------------------------------------------------------------
static le_data_ConnectionStateHandlerRef_t DataHandler;

//--------------------------------------------------------------------------------------------------
/**
 * Event ID on bootstrap connection failure.
 */
//--------------------------------------------------------------------------------------------------
static le_event_Id_t BsFailureEventId;

//--------------------------------------------------------------------------------------------------
/**
 * Convert an OMA FUMO (Firmware Update Management Object) error to an AVC error code
 */
//--------------------------------------------------------------------------------------------------
static le_avc_ErrorCode_t ConvertFumoErrorCode
(
    uint32_t fumoError
)
{
    switch (fumoError)
    {
        case 0:
            return LE_AVC_ERR_NONE;

        case LWM2MCORE_FUMO_CORRUPTED_PKG:
        case LWM2MCORE_FUMO_UNSUPPORTED_PKG:
            return LE_AVC_ERR_BAD_PACKAGE;

        case LWM2MCORE_FUMO_FAILED_VALIDATION:
            return LE_AVC_ERR_SECURITY_FAILURE;

        case LWM2MCORE_FUMO_INVALID_URI:
        case LWM2MCORE_FUMO_ALTERNATE_DL_ERROR:
        case LWM2MCORE_FUMO_NO_SUFFICIENT_MEMORY:
        default:
            return LE_AVC_ERR_INTERNAL;
    }
}

//--------------------------------------------------------------------------------------------------
/**
 *  Call back registered in LWM2M client for bearer related events
 */
//--------------------------------------------------------------------------------------------------
static void BearerEventCb
(
    bool connected,     ///< [IN] Indicates if the bearer is connected or disconnected
    void* contextPtr    ///< [IN] User data
)
{
    LE_INFO( "connected %d", connected);
    if (connected)
    {
        char endpointPtr[LWM2MCORE_ENDPOINT_LEN];
        bool result = false;

        /* Register objects to LWM2M and set the device endpoint:
         * - endpoint shall be unique for each client: IMEI/ESN/MEID
         * - the number of objects we will be passing through and the objects array
         */

        /* Get the device endpoint: IMEI */
        memset(endpointPtr, 0, LWM2MCORE_ENDPOINT_LEN);
        if (LE_OK != le_info_GetImei((char*)endpointPtr, (uint32_t) LWM2MCORE_ENDPOINT_LEN))
        {
            LE_ERROR("Error to retrieve the device IMEI");
        }
        else
        {
            /* Register to the LWM2M agent */
            if (!lwm2mcore_ObjectRegister(Lwm2mInstanceRef, endpointPtr, NULL, NULL))
            {
                LE_ERROR("ERROR in LWM2M obj reg");
            }
            else
            {
                result = lwm2mcore_Connect(Lwm2mInstanceRef);
                if (result != true)
                {
                    LE_ERROR("connect error");
                }
            }
        }
    }
    else
    {
        if (NULL != Lwm2mInstanceRef)
        {
            /* The data connection is closed */
            lwm2mcore_Free(Lwm2mInstanceRef);
            Lwm2mInstanceRef = NULL;

            /* Remove the data handler */
            le_data_RemoveConnectionStateHandler(DataHandler);
        }
    }
}

//--------------------------------------------------------------------------------------------------
/**
 * Callback for the connection state
 */
//--------------------------------------------------------------------------------------------------
static void ConnectionStateHandler
(
    const char* intfNamePtr,    ///< [IN] Interface name
    bool connected,             ///< [IN] connection state (true = connected, else false)
    void* contextPtr            ///< [IN] User data
)
{
    if (connected)
    {
        LE_DEBUG("Connected through interface '%s'", intfNamePtr);
        DataConnected = true;
        /* Call the callback */
        BearerEventCb(connected, contextPtr);
    }
    else
    {
        LE_WARN("Disconnected from data connection service, current state %d", DataConnected);
        if (DataConnected)
        {
            /* Call the callback */
            BearerEventCb(connected, contextPtr);
            DataConnected = false;
        }
    }
}

//--------------------------------------------------------------------------------------------------
/**
 * Callback for the LWM2M events linked to package download and update
 *
 * @return
 *      - 0 on success
 *      - negative value on failure.

 */
//--------------------------------------------------------------------------------------------------
static int PackageEventHandler
(
    lwm2mcore_Status_t status              ///< [IN] event status
)
{
    int result = 0;

    switch (status.event)
    {
        case LWM2MCORE_EVENT_PACKAGE_DOWNLOAD_DETAILS:
            // Notification of download pending is sent from user agreement callback.
            break;

        case LWM2MCORE_EVENT_DOWNLOAD_PROGRESS:
            if (LWM2MCORE_PKG_FW == status.u.pkgStatus.pkgType)
            {
                avcServer_UpdateHandler(LE_AVC_DOWNLOAD_IN_PROGRESS, LE_AVC_FIRMWARE_UPDATE,
                                        status.u.pkgStatus.numBytes, status.u.pkgStatus.progress,
                                        ConvertFumoErrorCode(status.u.pkgStatus.errorCode));
            }
            else if (LWM2MCORE_PKG_SW == status.u.pkgStatus.pkgType)
            {
                avcServer_UpdateHandler(LE_AVC_DOWNLOAD_IN_PROGRESS, LE_AVC_APPLICATION_UPDATE,
                                        status.u.pkgStatus.numBytes, status.u.pkgStatus.progress,
                                        ConvertFumoErrorCode(status.u.pkgStatus.errorCode));
            }
            else
            {
                LE_ERROR("Not yet supported package type %d", status.u.pkgStatus.pkgType);
            }
            break;

        case LWM2MCORE_EVENT_PACKAGE_DOWNLOAD_FINISHED:
            if (LWM2MCORE_PKG_FW == status.u.pkgStatus.pkgType)
            {
                avcServer_UpdateHandler(LE_AVC_DOWNLOAD_COMPLETE, LE_AVC_FIRMWARE_UPDATE,
                                        status.u.pkgStatus.numBytes, status.u.pkgStatus.progress,
                                        ConvertFumoErrorCode(status.u.pkgStatus.errorCode));
            }
            else if (LWM2MCORE_PKG_SW == status.u.pkgStatus.pkgType)
            {
                avcServer_UpdateHandler(LE_AVC_DOWNLOAD_COMPLETE, LE_AVC_APPLICATION_UPDATE,
                                        status.u.pkgStatus.numBytes, status.u.pkgStatus.progress,
                                        ConvertFumoErrorCode(status.u.pkgStatus.errorCode));
            }
            else
            {
                LE_ERROR("Not yet supported package download type %d",
                         status.u.pkgStatus.pkgType);
            }
            break;

        case LWM2MCORE_EVENT_PACKAGE_DOWNLOAD_FAILED:
            if (LWM2MCORE_PKG_FW == status.u.pkgStatus.pkgType)
            {
                avcServer_UpdateHandler(LE_AVC_DOWNLOAD_FAILED, LE_AVC_FIRMWARE_UPDATE,
                                        status.u.pkgStatus.numBytes, status.u.pkgStatus.progress,
                                        ConvertFumoErrorCode(status.u.pkgStatus.errorCode));
            }
            else if (LWM2MCORE_PKG_SW == status.u.pkgStatus.pkgType)
            {
                avcServer_UpdateHandler(LE_AVC_DOWNLOAD_FAILED, LE_AVC_APPLICATION_UPDATE,
                                        status.u.pkgStatus.numBytes, status.u.pkgStatus.progress,
                                        ConvertFumoErrorCode(status.u.pkgStatus.errorCode));
            }
            else
            {
                LE_ERROR("Not yet supported package type %d", status.u.pkgStatus.pkgType);
            }
            break;

        case LWM2MCORE_EVENT_UPDATE_STARTED:
            if (LWM2MCORE_PKG_FW == status.u.pkgStatus.pkgType)
            {
                avcServer_UpdateHandler(LE_AVC_INSTALL_IN_PROGRESS, LE_AVC_FIRMWARE_UPDATE,
                                        -1, -1, LE_AVC_ERR_NONE);
            }
            else if (LWM2MCORE_PKG_SW == status.u.pkgStatus.pkgType)
            {
                avcServer_UpdateHandler(LE_AVC_INSTALL_IN_PROGRESS, LE_AVC_APPLICATION_UPDATE,
                                        -1, -1, LE_AVC_ERR_NONE);
            }
            else
            {
                LE_ERROR("Not yet supported package type %d", status.u.pkgStatus.pkgType);
            }
            break;

        case LWM2MCORE_EVENT_UPDATE_FINISHED:
            if (LWM2MCORE_PKG_FW == status.u.pkgStatus.pkgType)
            {
                avcServer_UpdateHandler(LE_AVC_INSTALL_COMPLETE, LE_AVC_FIRMWARE_UPDATE,
                                        -1, -1, LE_AVC_ERR_NONE);
            }
            else if (LWM2MCORE_PKG_SW == status.u.pkgStatus.pkgType)
            {
                avcServer_UpdateHandler(LE_AVC_INSTALL_COMPLETE, LE_AVC_APPLICATION_UPDATE,
                                        -1, -1, LE_AVC_ERR_NONE);
            }
            else
            {
                LE_ERROR("Not yet supported package type %d", status.u.pkgStatus.pkgType);
            }
            break;

        case LWM2MCORE_EVENT_UPDATE_FAILED:
            if (LWM2MCORE_PKG_FW == status.u.pkgStatus.pkgType)
            {
                avcServer_UpdateHandler(LE_AVC_INSTALL_FAILED, LE_AVC_FIRMWARE_UPDATE,
                                        -1, -1, ConvertFumoErrorCode(status.u.pkgStatus.errorCode));
            }
            else if (LWM2MCORE_PKG_SW == status.u.pkgStatus.pkgType)
            {
                avcServer_UpdateHandler(LE_AVC_INSTALL_FAILED, LE_AVC_APPLICATION_UPDATE,
                                        -1, -1, ConvertFumoErrorCode(status.u.pkgStatus.errorCode));
            }
            else
            {
                LE_ERROR("Not yet supported package type %d", status.u.pkgStatus.pkgType);
            }
            break;

        default:
            if (LWM2MCORE_EVENT_LAST <= status.event)
            {
                LE_ERROR("unsupported event %d", status.event);
                result = -1;
            }
            break;
    }
    return result;
}

//--------------------------------------------------------------------------------------------------
/**
 * Callback for the LWM2M events
 *
 * @return
 *      - 0 on success
 *      - negative value on failure.

 */
//--------------------------------------------------------------------------------------------------
static int EventHandler
(
    lwm2mcore_Status_t status              ///< [IN] event status
)
{
    int result = 0;

    switch (status.event)
    {
        case LWM2MCORE_EVENT_SESSION_STARTED:
            LE_DEBUG("Session start");
            break;

        case LWM2MCORE_EVENT_SESSION_FAILED:
            LE_ERROR("Session failure");
            // If the device is connected to the bootstrap server, disconnect from server
            // If the device is connected to the DM server, a bootstrap connection will be
            // automatically initiated (session is not stopped)
            if (LE_AVC_BOOTSTRAP_SESSION == le_avc_GetSessionType())
            {
                LE_ERROR("Session failure on bootstrap server");
                le_event_Report(BsFailureEventId, NULL, 0);
            }
            break;

        case LWM2MCORE_EVENT_SESSION_FINISHED:
            LE_DEBUG("Session finished");
            avcServer_UpdateHandler(LE_AVC_SESSION_STOPPED, LE_AVC_UNKNOWN_UPDATE,
                                    -1, -1, LE_AVC_ERR_NONE);
            break;

        case LWM2MCORE_EVENT_LWM2M_SESSION_TYPE_START:
            if (status.u.session.type == LWM2MCORE_SESSION_BOOTSTRAP)
            {
                LE_DEBUG("Connected to bootstrap");
            }
            else
            {
                LE_DEBUG("Connected to DM");
                avcServer_UpdateHandler(LE_AVC_SESSION_STARTED, LE_AVC_UNKNOWN_UPDATE,
                                        -1, -1, LE_AVC_ERR_NONE);
            }
            break;

        case LWM2MCORE_EVENT_PACKAGE_DOWNLOAD_DETAILS:
        case LWM2MCORE_EVENT_DOWNLOAD_PROGRESS:
        case LWM2MCORE_EVENT_PACKAGE_DOWNLOAD_FINISHED:
        case LWM2MCORE_EVENT_PACKAGE_DOWNLOAD_FAILED:
        case LWM2MCORE_EVENT_UPDATE_STARTED:
        case LWM2MCORE_EVENT_UPDATE_FINISHED:
        case LWM2MCORE_EVENT_UPDATE_FAILED:
            result = PackageEventHandler(status);
            break;

        default:
            if (LWM2MCORE_EVENT_LAST <= status.event)
            {
                LE_ERROR("unsupported event %d", status.event);
                result = -1;
            }
            break;
    }

    return result;
}

//--------------------------------------------------------------------------------------------------
/**
 * Connect to the server
 *
 * @return
 *      - LE_OK in case of success
 *      - LE_FAULT in case of failure
 */
//--------------------------------------------------------------------------------------------------
le_result_t avcClient_Connect
(
    void
)
{
    le_result_t result = LE_FAULT;

    if (NULL == Lwm2mInstanceRef)
    {
        Lwm2mInstanceRef = lwm2mcore_Init(EventHandler);

        /* Initialize the bearer */
        /* Open a data connection */
        le_data_ConnectService();

        DataHandler = le_data_AddConnectionStateHandler(ConnectionStateHandler, NULL);

        /* Request data connection */
        DataRef = le_data_Request();
        if (NULL != DataRef)
        {
            result = LE_OK;
        }
    }
    return result;
}

//--------------------------------------------------------------------------------------------------
/**
 * LWM2M client entry point to close a connection
 *
 * @return
 *      - LE_OK in case of success
 *      - LE_FAULT in case of failure
 */
//--------------------------------------------------------------------------------------------------
le_result_t avcClient_Disconnect
(
    void
)
{
    LE_DEBUG("Disconnect");
    le_result_t result = LE_FAULT;

    /* If the LWM2MCORE_TIMER_STEP timer is running, this means that a connection is active */
    if (true == lwm2mcore_TimerIsRunning(LWM2MCORE_TIMER_STEP))
    {
        if (true == lwm2mcore_Disconnect(Lwm2mInstanceRef))
        {
            /* stop the bearer */
            /* Check that a data connection was opened */
            if (NULL != DataRef)
            {
                /* Close the data connection */
                le_data_Release(DataRef);
            }
            /* The data connection is closed */
            lwm2mcore_Free(Lwm2mInstanceRef);
            Lwm2mInstanceRef = NULL;

            /* Remove the data handler */
            le_data_RemoveConnectionStateHandler(DataHandler);

            result = LE_OK;
        }
    }

    return result;
}

//--------------------------------------------------------------------------------------------------
/**
 * LWM2M client entry point to send a registration update
 *
 * @return
 *      - LE_OK in case of success
 *      - LE_FAULT in case of failure
 */
//--------------------------------------------------------------------------------------------------
le_result_t avcClient_Update
(
    void
)
{
    LE_DEBUG("Registration update");

    if (true == lwm2mcore_Update(Lwm2mInstanceRef))
    {
        return LE_OK;
    }
    else
    {
        return LE_FAULT;
    }
}

//--------------------------------------------------------------------------------------------------
/**
 * LWM2M client entry point to push data
 *
 * @return
 *      - LE_OK in case of success
 *      - LE_BUSY if busy pushing data
 *      - LE_FAULT in case of failure
 */
//--------------------------------------------------------------------------------------------------
le_result_t avcClient_Push
(
    uint8_t* payload,
    size_t payloadLength,
    lwm2mcore_PushContent_t contentType,
    uint16_t* midPtr
)
{
    LE_DEBUG("Push data");

    lwm2mcore_PushResult_t rc = lwm2mcore_Push(Lwm2mInstanceRef,
                                               payload,
                                               payloadLength,
                                               contentType,
                                               midPtr);

    switch (rc)
    {
        case LWM2MCORE_PUSH_INITIATED:
            return LE_OK;
        case LWM2MCORE_PUSH_BUSY:
            return LE_BUSY;
        case LWM2MCORE_PUSH_FAILED:
        default:
            return LE_FAULT;
    }
}

//--------------------------------------------------------------------------------------------------
/**
 * Send instances of object 9 and the Legato objects for all currently installed applications.
 *
 */
//--------------------------------------------------------------------------------------------------
void avcClient_SendList
(
    char* lwm2mObjListPtr,      ///< [IN] Object instances list
    size_t objListLen           ///< [IN] List length
)
{
    lwm2mcore_UpdateSwList(Lwm2mInstanceRef, lwm2mObjListPtr, objListLen);
}

//--------------------------------------------------------------------------------------------------
/**
 * Returns the instance reference of this client
 */
//--------------------------------------------------------------------------------------------------
lwm2mcore_Ref_t avcClient_GetInstance
(
    void
)
{
    return Lwm2mInstanceRef;
}

//--------------------------------------------------------------------------------------------------
/**
 * LWM2M client entry point to get session status
 *
 * @return
 *      - LE_AVC_DM_SESSION when the device is connected to the DM server
 *      - LE_AVC_BOOTSTRAP_SESSION when the device is connected to the BS server
 *      - LE_AVC_SESSION_INVALID in other cases
 */
//--------------------------------------------------------------------------------------------------
le_avc_SessionType_t avcClient_GetSessionType
(
    void
)
{
    bool isDeviceManagement = false;

    if (lwm2mcore_ConnectionGetType(Lwm2mInstanceRef, &isDeviceManagement))
    {
        return (isDeviceManagement ? LE_AVC_DM_SESSION : LE_AVC_BOOTSTRAP_SESSION);
    }
    return LE_AVC_SESSION_INVALID;
}

//--------------------------------------------------------------------------------------------------
/**
 *  Handler to terminate a connection to bootstrap on failure
 */
//--------------------------------------------------------------------------------------------------
void BsFailureHandler
(
    void* reportPtr
)
{
    lwm2mcore_Disconnect(Lwm2mInstanceRef);
}

//--------------------------------------------------------------------------------------------------
/**
 * Initialization function avcClient. Should be called only once.
 */
//--------------------------------------------------------------------------------------------------
void avcClient_Init
(
   void
)
{
    BsFailureEventId = le_event_CreateId("BsFailure", 0);
    le_event_AddHandler("BsFailureHandler", BsFailureEventId, BsFailureHandler);
}
