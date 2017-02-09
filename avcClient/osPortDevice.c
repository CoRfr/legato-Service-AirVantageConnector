/**
 * @file osPortDevice.c
 *
 * Porting layer for device parameters
 *
 * Copyright (C) Sierra Wireless Inc.
 *
 */

#include "legato.h"
#include "interfaces.h"
#include "osPortDevice.h"
#include "assetData.h"
#include "avcUpdateShared.h" // For MAX_VERSION_STR_BYTES
#include <sys/utsname.h>

//--------------------------------------------------------------------------------------------------
/**
 * Define for LK version length
 */
//--------------------------------------------------------------------------------------------------
#define LK_VERSION_LENGTH   10

//--------------------------------------------------------------------------------------------------
/**
 * Define for FW version buffer length
 */
//--------------------------------------------------------------------------------------------------
#define FW_BUFFER_LENGTH    512

//--------------------------------------------------------------------------------------------------
/**
 * Define for unknown version
 */
//--------------------------------------------------------------------------------------------------
#define UNKNOWN_VERSION     "unknown"

//--------------------------------------------------------------------------------------------------
/**
 * Define for modem tag in FW version string
 */
//--------------------------------------------------------------------------------------------------
#define MODEM_TAG "MDM_"

//--------------------------------------------------------------------------------------------------
/**
 * Define for LK tag in FW version string
 */
//--------------------------------------------------------------------------------------------------
#define LK_TAG "_LK_"

//--------------------------------------------------------------------------------------------------
/**
 * Define for modem tag in Linux version string
 */
//--------------------------------------------------------------------------------------------------
#define LINUX_TAG "_OS_"

//--------------------------------------------------------------------------------------------------
/**
 * Define for root FS tag in FW version string
 */
//--------------------------------------------------------------------------------------------------
#define ROOT_FS_TAG "_RFS_"

//--------------------------------------------------------------------------------------------------
/**
 * Define for user FS tag in FW version string
 */
//--------------------------------------------------------------------------------------------------
#define USER_FS_TAG "_UFS_"

//--------------------------------------------------------------------------------------------------
/**
 * Define for Legato tag in FW version string
 */
//--------------------------------------------------------------------------------------------------
#define LEGATO_TAG "_LE_"

//--------------------------------------------------------------------------------------------------
/**
 * Define for PRI tag in FW version string
 */
//--------------------------------------------------------------------------------------------------
#define PRI_TAG "_PRI_"

//--------------------------------------------------------------------------------------------------
 /**
 *  Path to the file that stores the Legato version number string.
 */
//--------------------------------------------------------------------------------------------------
#define LEGATO_VERSION_FILE "/legato/systems/current/version"

//--------------------------------------------------------------------------------------------------
 /**
 *  Path to the file that stores the LK version number string.
 */
//--------------------------------------------------------------------------------------------------
#define LK_VERSION_FILE "/proc/cmdline"

//--------------------------------------------------------------------------------------------------
 /**
 *  Path to the file that stores the root FS version number string.
 */
//--------------------------------------------------------------------------------------------------
#define RFS_VERSION_FILE "/etc/rootfsver.txt"

//--------------------------------------------------------------------------------------------------
 /**
 *  String to be check in file which stores the LK version
 */
//--------------------------------------------------------------------------------------------------
#define LK_STRING_FILE "lkversion="

//--------------------------------------------------------------------------------------------------
 /**
 *  Define of space
 */
//--------------------------------------------------------------------------------------------------
#define SPACE " "

//--------------------------------------------------------------------------------------------------
/**
 * Function pointer to get a component version
 *
 * @return
 *      - written buffer length
 */
//--------------------------------------------------------------------------------------------------
typedef size_t (*getVersion_t)
(
    char* versionBufferPtr,         ///< [INOUT] Buffer to hold the string.
    size_t lenPtr                   ///< [IN] Buffer length
);

//--------------------------------------------------------------------------------------------------
/**
 * Struture to get a component version and its corresponding tag for the FW version string
 */
//--------------------------------------------------------------------------------------------------
typedef struct
{
    char* tagPtr;               ///< Component tag
    getVersion_t funcPtr;       ///< Function to read the component version
}ComponentVersion_t;

//--------------------------------------------------------------------------------------------------
/**
 * Attempt to read the Modem version string
 *
 * @return
 *      - written buffer length
 */
//--------------------------------------------------------------------------------------------------
size_t GetModemVersion
(
    char* versionBufferPtr,         ///< [INOUT] Buffer to hold the string.
    size_t len                      ///< [IN] Buffer length
)
{
    size_t returnedLen = 0;
    if (NULL != versionBufferPtr)
    {
        char tmpModemBufferPtr[FW_BUFFER_LENGTH];
        if (LE_OK == le_info_GetFirmwareVersion(tmpModemBufferPtr, FW_BUFFER_LENGTH))
        {
            char* savePtr;
            char* tmpBufferPtr = strtok_r(tmpModemBufferPtr, SPACE, &savePtr);
            if (NULL != tmpBufferPtr)
            {
                snprintf(versionBufferPtr, len, "%s", tmpBufferPtr);
                returnedLen = strlen(versionBufferPtr);
            }
            else
            {
                snprintf(versionBufferPtr, len, UNKNOWN_VERSION, strlen(UNKNOWN_VERSION));
                returnedLen = strlen(versionBufferPtr);
            }
        }
        else
        {
            snprintf(versionBufferPtr, len, UNKNOWN_VERSION, strlen(UNKNOWN_VERSION));
            returnedLen = strlen(versionBufferPtr);
        }
        LE_INFO("Modem version = %s, returnedLen %d", versionBufferPtr, returnedLen);
    }
    return returnedLen;
}

//--------------------------------------------------------------------------------------------------
/**
 * Attempt to read the LK version string from the file system.
 *
 * @return
 *      - written buffer length
 */
//--------------------------------------------------------------------------------------------------
size_t GetLkVersion
(
    char* versionBufferPtr,         ///< [INOUT] Buffer to hold the string.
    size_t len                      ///< [IN] Buffer length
)
{
    size_t returnedLen = 0;
    if (NULL != versionBufferPtr)
    {
        char tmpLkBufferPtr[FW_BUFFER_LENGTH];
        char* tokenPtr;
        char* savePtr;
        FILE* fpPtr;
        fpPtr = fopen(LK_VERSION_FILE, "r");
        if ((NULL != fpPtr)
         && (NULL != fgets(tmpLkBufferPtr, FW_BUFFER_LENGTH, fpPtr)))
        {
            tokenPtr = strtok_r(tmpLkBufferPtr, SPACE, &savePtr);
            /* Look for "lkversion=" */
            while (NULL != tokenPtr)
            {
                tokenPtr = strtok_r(NULL, SPACE, &savePtr);
                if (NULL == tokenPtr)
                {
                    snprintf(versionBufferPtr,
                             len,
                             UNKNOWN_VERSION,
                             strlen(UNKNOWN_VERSION));
                    returnedLen = strlen(versionBufferPtr);
                    break;
                }
                if (0 == strncmp(tokenPtr, LK_STRING_FILE, LK_VERSION_LENGTH))
                {
                    tokenPtr += LK_VERSION_LENGTH;
                    snprintf(versionBufferPtr, len, "%s", tokenPtr);
                    returnedLen = strlen(versionBufferPtr);
                    break;
                }
            }
        }
        else
        {
            snprintf(versionBufferPtr, len, UNKNOWN_VERSION, strlen(UNKNOWN_VERSION));
            returnedLen = strlen(versionBufferPtr);
        }

        if (NULL != fpPtr)
        {
            fclose(fpPtr);
        }
        LE_INFO("lkVersion %s, len %d", versionBufferPtr, returnedLen);
    }
    return returnedLen;
}

//--------------------------------------------------------------------------------------------------
/**
 * Attempt to read the Linux version string from the file system.
 *
 * @return
 *      - written buffer length
 */
//--------------------------------------------------------------------------------------------------
size_t GetOsVersion
(
    char* versionBufferPtr,         ///< [INOUT] Buffer to hold the string.
    size_t len                      ///< [IN] Buffer length
)
{
    size_t returnedLen = 0;
    if (NULL != versionBufferPtr)
    {
        struct utsname linuxInfo;
        if (0 == uname(&linuxInfo))
        {
            LE_INFO("Linux Version: %s", linuxInfo.release);
            snprintf(versionBufferPtr, len, linuxInfo.release, strlen(linuxInfo.release));
            returnedLen = strlen(versionBufferPtr);
        }
        else
        {
            snprintf(versionBufferPtr, len, UNKNOWN_VERSION, strlen(UNKNOWN_VERSION));
            returnedLen = strlen(versionBufferPtr);
        }
        LE_INFO("OsVersion %s, len %d", versionBufferPtr, returnedLen);
    }
    return returnedLen;
}

//--------------------------------------------------------------------------------------------------
/**
 * Attempt to read the root FS version string from the file system.
 *
 * @return
 *      - written buffer length
 */
//--------------------------------------------------------------------------------------------------
size_t GetRfsVersion
(
    char* versionBufferPtr,         ///< [INOUT] Buffer to hold the string.
    size_t len                      ///< [IN] Buffer length
)
{
    size_t returnedLen = 0;
    if (NULL != versionBufferPtr)
    {
        char tmpRfsBufferPtr[FW_BUFFER_LENGTH];
        FILE* fpPtr;
        fpPtr = fopen(RFS_VERSION_FILE, "r");
        if ((NULL != fpPtr)
         && (NULL != fgets(tmpRfsBufferPtr, FW_BUFFER_LENGTH, fpPtr)))
        {
            char* savePtr;
            char* tmpBufferPtr = strtok_r(tmpRfsBufferPtr, SPACE, &savePtr);
            if (NULL != tmpBufferPtr)
            {
                snprintf(versionBufferPtr, len, "%s", tmpBufferPtr);
                returnedLen = strlen(versionBufferPtr);
            }
            else
            {
                snprintf(versionBufferPtr, len, UNKNOWN_VERSION, strlen(UNKNOWN_VERSION));
                returnedLen = strlen(versionBufferPtr);
            }
        }
        else
        {
            snprintf(versionBufferPtr, len, UNKNOWN_VERSION, strlen(UNKNOWN_VERSION));
            returnedLen = strlen(versionBufferPtr);
        }

        if (NULL != fpPtr)
        {
            fclose(fpPtr);
        }
        LE_INFO("RfsVersion %s, len %d", versionBufferPtr, returnedLen);
    }
    return returnedLen;
}

//--------------------------------------------------------------------------------------------------
/**
 * Attempt to read the user FS version string from the file system.
 *
 * @return
 *      - written buffer length
 */
//--------------------------------------------------------------------------------------------------
size_t GetUfsVersion
(
    char* versionBufferPtr,         ///< [INOUT] Buffer to hold the string.
    size_t len                      ///< [IN] Buffer length
)
{
    size_t returnedLen = 0;
    if (NULL != versionBufferPtr)
    {
        snprintf(versionBufferPtr, len, UNKNOWN_VERSION, strlen(UNKNOWN_VERSION));
        returnedLen = strlen(versionBufferPtr);
        LE_INFO("UfsVersion %s, len %d", versionBufferPtr, returnedLen);
    }
    return returnedLen;
}

//--------------------------------------------------------------------------------------------------
/**
 * Attempt to read the Legato version string from the file system.
 *
 * @return
 *      - written buffer length
 */
//--------------------------------------------------------------------------------------------------
size_t GetLegatoVersion
(
    char* versionBufferPtr,         ///< [INOUT] Buffer to hold the string.
    size_t len                      ///< [IN] Buffer length
)
{
    size_t returnedLen = 0;
    if (NULL != versionBufferPtr)
    {
        FILE* versionFilePtr = fopen(LEGATO_VERSION_FILE, "r");
        if (NULL == versionFilePtr)
        {
            LE_INFO("Could not open Legato version file.");
            snprintf(versionBufferPtr, len, UNKNOWN_VERSION, strlen(UNKNOWN_VERSION));
            returnedLen = strlen(versionBufferPtr);
            return returnedLen;
        }

        if (fgets(versionBufferPtr, MAX_VERSION_STR_BYTES, versionFilePtr) != NULL)
        {
            char* newLinePtr = strchr(versionBufferPtr, '\n');
            if (NULL != newLinePtr)
            {
                *newLinePtr = '\0';
            }
            returnedLen = strlen(versionBufferPtr);
        }
        else
        {
            LE_INFO("Could not read Legato version.");
            snprintf(versionBufferPtr, len, UNKNOWN_VERSION, strlen(UNKNOWN_VERSION));
            returnedLen = strlen(versionBufferPtr);
        }
        fclose(versionFilePtr);
        LE_INFO("Legato version = %s, len %d", versionBufferPtr, returnedLen);
    }
    return returnedLen;
}

//--------------------------------------------------------------------------------------------------
/**
 * Attempt to read the PRI version string from the file system.
 *
 * @return
 *      - written buffer length
 */
//--------------------------------------------------------------------------------------------------
size_t GetPriVersion
(
    char* versionBufferPtr,         ///< [INOUT] Buffer to hold the string.
    size_t len                      ///< [IN] Buffer length
)
{
    size_t returnedLen = 0;
    if (NULL != versionBufferPtr)
    {
        char priIdPn[LE_INFO_MAX_PRIID_PN_BYTES];
        char priIdRev[LE_INFO_MAX_PRIID_REV_BYTES];

        if (LE_OK == le_info_GetPriId(priIdPn, LE_INFO_MAX_PRIID_PN_BYTES,
                                      priIdRev, LE_INFO_MAX_PRIID_REV_BYTES))
        {
            if (strlen(priIdPn) && strlen(priIdRev))
            {
                snprintf(versionBufferPtr, len, "%s-%s", priIdPn, priIdRev);
                returnedLen = strlen(versionBufferPtr);
            }
            else
            {
                snprintf(versionBufferPtr, len, UNKNOWN_VERSION, strlen(UNKNOWN_VERSION));
                returnedLen = strlen(versionBufferPtr);
            }
        }
        else
        {
            snprintf(versionBufferPtr, len, UNKNOWN_VERSION, strlen(UNKNOWN_VERSION));
            returnedLen = strlen(versionBufferPtr);
        }
        LE_INFO("PriVersion %s, len %d", versionBufferPtr, returnedLen);
    }
    return returnedLen;
}

//--------------------------------------------------------------------------------------------------
/**
 *  Components on which version needs to be retrieved
 */
//--------------------------------------------------------------------------------------------------
const ComponentVersion_t VersionInfo[] =
{
  { MODEM_TAG,      GetModemVersion },
  { LK_TAG,         GetLkVersion },
  { LINUX_TAG,      GetOsVersion },
  { ROOT_FS_TAG,    GetRfsVersion },
  { USER_FS_TAG,    GetUfsVersion },
  { LEGATO_TAG,     GetLegatoVersion },
  { PRI_TAG,        GetPriVersion }
};

//--------------------------------------------------------------------------------------------------
/**
 *                  OBJECT 3: DEVICE
 */
//--------------------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------------------
/**
 * Retrieve the device manufacturer
 * This API treatment needs to have a procedural treatment
 *
 * @return
 *      - LWM2MCORE_ERR_COMPLETED_OK if the treatment succeeds
 *      - LWM2MCORE_ERR_GENERAL_ERROR if the treatment fails
 *      - LWM2MCORE_ERR_INCORRECT_RANGE if the provided parameters (WRITE operation) is incorrect
 *      - LWM2MCORE_ERR_NOT_YET_IMPLEMENTED if the resource is not yet implemented
 *      - LWM2MCORE_ERR_OP_NOT_SUPPORTED  if the resource is not supported
 *      - LWM2MCORE_ERR_INVALID_ARG if a parameter is invalid in resource handler
 *      - LWM2MCORE_ERR_INVALID_STATE in case of invalid state to treat the resource handler
 */
//--------------------------------------------------------------------------------------------------
lwm2mcore_sid_t os_portDeviceManufacturer
(
    char *bufferPtr,                        ///< [INOUT] data buffer
    size_t *lenPtr                          ///< [INOUT] length of input buffer and length of the
                                            ///< returned data
)
{
    lwm2mcore_sid_t result;

    if ((bufferPtr == NULL) || (lenPtr == NULL))
    {
        result = LWM2MCORE_ERR_INVALID_ARG;
    }
    else
    {
        le_result_t leresult = le_info_GetManufacturerName((char*)bufferPtr, (uint32_t) *lenPtr);

        switch (leresult)
        {
            case LE_OK:
                result = LWM2MCORE_ERR_COMPLETED_OK;
                break;

            case LE_OVERFLOW:
                result = LWM2MCORE_ERR_OVERFLOW;
                break;

            case LE_FAULT:
            default:
                result = LWM2MCORE_ERR_GENERAL_ERROR;
                break;
        }
    }
    LE_INFO("os_portDeviceManufacturer result %d", result);
    return result;
}

//--------------------------------------------------------------------------------------------------
/**
 * Retrieve the device model number
 * This API treatment needs to have a procedural treatment
 *
 * @return
 *      - LWM2MCORE_ERR_COMPLETED_OK if the treatment succeeds
 *      - LWM2MCORE_ERR_GENERAL_ERROR if the treatment fails
 *      - LWM2MCORE_ERR_INCORRECT_RANGE if the provided parameters (WRITE operation) is incorrect
 *      - LWM2MCORE_ERR_NOT_YET_IMPLEMENTED if the resource is not yet implemented
 *      - LWM2MCORE_ERR_OP_NOT_SUPPORTED  if the resource is not supported
 *      - LWM2MCORE_ERR_INVALID_ARG if a parameter is invalid in resource handler
 *      - LWM2MCORE_ERR_INVALID_STATE in case of invalid state to treat the resource handler
 */
//--------------------------------------------------------------------------------------------------
lwm2mcore_sid_t os_portDeviceModelNumber
(
    char *bufferPtr,                        ///< [INOUT] data buffer
    size_t *lenPtr                          ///< [INOUT] length of input buffer and length of the
                                            ///< returned data
)
{
    lwm2mcore_sid_t result;

    if ((bufferPtr == NULL) || (lenPtr == NULL))
    {
        result = LWM2MCORE_ERR_INVALID_ARG;
    }
    else
    {
        le_result_t leresult = le_info_GetDeviceModel((char*)bufferPtr, (uint32_t) *lenPtr);

        switch (leresult)
        {
            case LE_OVERFLOW:
                result = LWM2MCORE_ERR_OVERFLOW;
                break;

            case LE_OK:
                result = LWM2MCORE_ERR_COMPLETED_OK;
                break;

            case LE_FAULT:
            default:
                result = LWM2MCORE_ERR_GENERAL_ERROR;
                break;
        }
    }
    return result;
}

//--------------------------------------------------------------------------------------------------
/**
 * Retrieve the device serial number
 * This API treatment needs to have a procedural treatment
 *
 * @return
 *      - LWM2MCORE_ERR_COMPLETED_OK if the treatment succeeds
 *      - LWM2MCORE_ERR_GENERAL_ERROR if the treatment fails
 *      - LWM2MCORE_ERR_INCORRECT_RANGE if the provided parameters (WRITE operation) is incorrect
 *      - LWM2MCORE_ERR_NOT_YET_IMPLEMENTED if the resource is not yet implemented
 *      - LWM2MCORE_ERR_OP_NOT_SUPPORTED  if the resource is not supported
 *      - LWM2MCORE_ERR_INVALID_ARG if a parameter is invalid in resource handler
 *      - LWM2MCORE_ERR_INVALID_STATE in case of invalid state to treat the resource handler
 */
//--------------------------------------------------------------------------------------------------
lwm2mcore_sid_t os_portDeviceSerialNumber
(
    char* bufferPtr,            ///< [INOUT] data buffer
    size_t* lenPtr              ///< [INOUT] length of input buffer and length of the returned data
)
{
    lwm2mcore_sid_t result;

    if ((bufferPtr == NULL) || (lenPtr == NULL))
    {
        result = LWM2MCORE_ERR_INVALID_ARG;
    }
    else
    {
        le_result_t leresult = le_info_GetPlatformSerialNumber((char*)bufferPtr,
                                                               (uint32_t) *lenPtr);

        switch (leresult)
        {
            case LE_OVERFLOW:
                result = LWM2MCORE_ERR_OVERFLOW;
                break;

            case LE_OK:
                result = LWM2MCORE_ERR_COMPLETED_OK;
                break;

            case LE_FAULT:
            default:
                result = LWM2MCORE_ERR_GENERAL_ERROR;
                break;
        }
    }
    return result;
}

//--------------------------------------------------------------------------------------------------
/**
 * Retrieve the firmware version
 * This API treatment needs to have a procedural treatment
 *
 * @return
 *      - LWM2MCORE_ERR_COMPLETED_OK if the treatment succeeds
 *      - LWM2MCORE_ERR_GENERAL_ERROR if the treatment fails
 *      - LWM2MCORE_ERR_INCORRECT_RANGE if the provided parameters (WRITE operation) is incorrect
 *      - LWM2MCORE_ERR_NOT_YET_IMPLEMENTED if the resource is not yet implemented
 *      - LWM2MCORE_ERR_OP_NOT_SUPPORTED  if the resource is not supported
 *      - LWM2MCORE_ERR_INVALID_ARG if a parameter is invalid in resource handler
 *      - LWM2MCORE_ERR_INVALID_STATE in case of invalid state to treat the resource handler
 *      - LWM2MCORE_ERR_OVERFLOW in case of buffer overflow
 */
//--------------------------------------------------------------------------------------------------
lwm2mcore_sid_t os_portDeviceFirmwareVersion
(
    char* bufferPtr,            ///< [INOUT] data buffer
    size_t* lenPtr              ///< [INOUT] length of input buffer and length of the returned data
)
{
    lwm2mcore_sid_t result;
    if ((bufferPtr == NULL) || (lenPtr == NULL))
    {
        result = LWM2MCORE_ERR_INVALID_ARG;
    }
    else
    {
        char tmpBufferPtr[FW_BUFFER_LENGTH];
        uint32_t remainingLen = *lenPtr;
        size_t len;
        uint32_t i = 0;

        LE_INFO("remainingLen %d", remainingLen);

        for (i = 0; i < NUM_ARRAY_MEMBERS(VersionInfo); i++)
        {
            if (NULL != VersionInfo[i].funcPtr)
            {
                len = VersionInfo[i].funcPtr(tmpBufferPtr, FW_BUFFER_LENGTH);
                LE_INFO("len %d - remainingLen %d", len, remainingLen);
                /* len doesn't contain the final \0
                 * remainingLen contains the final \0
                 * So we have to keep one byte for \0
                 */
                if (len > (remainingLen - 1))
                {
                    *lenPtr = 0;
                    bufferPtr[*lenPtr] = '\0';
                    return LWM2MCORE_ERR_OVERFLOW;
                }
                else
                {
                    snprintf(bufferPtr + strlen(bufferPtr),
                             remainingLen,
                             "%s%s",
                             VersionInfo[i].tagPtr,
                             tmpBufferPtr);
                    remainingLen -= len;
                    LE_INFO("remainingLen %d", remainingLen);
                }
            }
        }
        *lenPtr = strlen(bufferPtr);
        result = LWM2MCORE_ERR_COMPLETED_OK;
    }
    return result;
}

//--------------------------------------------------------------------------------------------------
/**
 * Retrieve the device time
 * This API treatment needs to have a procedural treatment
 *
 * @return
 *      - LWM2MCORE_ERR_COMPLETED_OK if the treatment succeeds
 *      - LWM2MCORE_ERR_GENERAL_ERROR if the treatment fails
 *      - LWM2MCORE_ERR_INCORRECT_RANGE if the provided parameters (WRITE operation) is incorrect
 *      - LWM2MCORE_ERR_NOT_YET_IMPLEMENTED if the resource is not yet implemented
 *      - LWM2MCORE_ERR_OP_NOT_SUPPORTED  if the resource is not supported
 *      - LWM2MCORE_ERR_INVALID_ARG if a parameter is invalid in resource handler
 *      - LWM2MCORE_ERR_INVALID_STATE in case of invalid state to treat the resource handler
 */
//--------------------------------------------------------------------------------------------------
lwm2mcore_sid_t os_portDeviceCurrentTime
(
    uint64_t* valuePtr                      ///< [INOUT] data buffer
)
{
    lwm2mcore_sid_t result;

    if (valuePtr == NULL)
    {
        result = LWM2MCORE_ERR_INVALID_ARG;
    }
    else
    {
        le_clk_Time_t t = le_clk_GetAbsoluteTime();
        *valuePtr = 0;
        LE_INFO("time %d", t.sec);
        if (0 == t.sec)
        {
            result = LWM2MCORE_ERR_GENERAL_ERROR;
        }
        else
        {
            *valuePtr = t.sec;
            result = LWM2MCORE_ERR_COMPLETED_OK;
        }
    }
    return result;
}
