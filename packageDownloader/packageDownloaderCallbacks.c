/**
 * @file packageDownloaderCallbacks.c
 *
 * <HR>
 *
 * Copyright (C) Sierra Wireless Inc.
 *
 */

#include <legato.h>
#include <curl/curl.h>
#include <lwm2mcore/update.h>
#include <lwm2mcorePackageDownloader.h>
#include <interfaces.h>
#include <avcFs.h>
#include <avcFsConfig.h>
#include "packageDownloader.h"
#include "packageDownloaderCallbacks.h"
#include "avcServer.h"

//--------------------------------------------------------------------------------------------------
/**
 * Value of 1 mebibyte in bytes
 */
//--------------------------------------------------------------------------------------------------
#define MEBIBYTE            (1 << 20)

//--------------------------------------------------------------------------------------------------
/**
 * HTTP status codes
 */
//--------------------------------------------------------------------------------------------------
#define NOT_FOUND               404
#define INTERNAL_SERVER_ERROR   500
#define BAD_GATEWAY             502
#define SERVICE_UNAVAILABLE     503

//--------------------------------------------------------------------------------------------------
/**
 * Max string buffer size
 */
//--------------------------------------------------------------------------------------------------
#define BUF_SIZE  512

//--------------------------------------------------------------------------------------------------
/**
 * PackageInfo data structure.
 */
//--------------------------------------------------------------------------------------------------
typedef struct
{
    double  totalSize;              ///< total file size
    long    httpRespCode;           ///< HTTP response code
    char    curlVersion[BUF_SIZE];  ///< libcurl version
}
PackageInfo_t;

//--------------------------------------------------------------------------------------------------
/**
 * Package data structure
 */
//--------------------------------------------------------------------------------------------------
typedef struct
{
    CURL*           curlPtr;    ///< curl pointer
    const char*     uriPtr;     ///< package URI pointer
    PackageInfo_t   pkgInfo;    ///< package information
}
Package_t;

//--------------------------------------------------------------------------------------------------
/**
 * Dummy write callback
 */
//--------------------------------------------------------------------------------------------------
static size_t WriteDummy
(
    void*   contentsPtr,
    size_t  size,
    size_t  nmemb,
    void*   streamPtr
)
{
    size_t count = size * nmemb;

    return count;
}

//--------------------------------------------------------------------------------------------------
/**
 * Write downloaded data to memory chunk
 */
//--------------------------------------------------------------------------------------------------
static size_t Write
(
    void*   contentsPtr,
    size_t  size,
    size_t  nmemb,
    void*   streamPtr
)
{
    size_t count = size * nmemb;

    // Check if the download should be aborted
    if (true == packageDownloader_CurrentDownloadToAbort())
    {
        LE_ERROR("Download aborted");
        return 0;
    }

    // Check if the download should be suspended
    if (true == packageDownloader_CheckDownloadToSuspend())
    {
        LE_ERROR("Download suspended");
        return 0;
    }

    // Process the downloaded data
    if (DWL_OK != lwm2mcore_PackageDownloaderReceiveData(contentsPtr, count))
    {
        LE_ERROR("Data processing stopped by DWL parser");
        return 0;
    }

    return count;
}

//--------------------------------------------------------------------------------------------------
/**
 * Check HTTP status codes
 */
//--------------------------------------------------------------------------------------------------
static int CheckHttpStatusCode
(
    long code
)
{
    int ret = 0;

    switch (code)
    {
        case NOT_FOUND:
            LE_DEBUG("404 - NOT FOUND");
            ret = -1;
            break;
        case INTERNAL_SERVER_ERROR:
            LE_DEBUG("500 - INTERNAL SERVER ERROR");
            ret = -1;
            break;
        case BAD_GATEWAY:
            LE_DEBUG("502 - BAD GATEWAY");
            ret = -1;
            break;
        case SERVICE_UNAVAILABLE:
            LE_DEBUG("503 - SERVICE UNAVAILABLE");
            ret = -1;
            break;
        default: break;
    }

    return ret;
}

//--------------------------------------------------------------------------------------------------
/**
 * Get download information
 */
//--------------------------------------------------------------------------------------------------
static int GetDownloadInfo
(
    Package_t* pkgPtr
)
{
    CURLcode rc;
    PackageInfo_t* pkgInfoPtr;

    pkgInfoPtr = &pkgPtr->pkgInfo;

    // just get the header, will always succeed
    curl_easy_setopt(pkgPtr->curlPtr, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(pkgPtr->curlPtr, CURLOPT_WRITEFUNCTION, WriteDummy);

    rc = curl_easy_perform(pkgPtr->curlPtr);
    if (CURLE_OK != rc)
    {
        LE_ERROR("failed to perform curl request: %s", curl_easy_strerror(rc));
        return -1;
    }

    // check for a valid response
    rc = curl_easy_getinfo(pkgPtr->curlPtr, CURLINFO_RESPONSE_CODE, &pkgInfoPtr->httpRespCode);
    if (CURLE_OK != rc)
    {
        LE_ERROR("failed to get response code: %s", curl_easy_strerror(rc));
        return -1;
    }

    rc = curl_easy_getinfo(pkgPtr->curlPtr, CURLINFO_CONTENT_LENGTH_DOWNLOAD,
                           &pkgInfoPtr->totalSize);
    if (CURLE_OK != rc)
    {
        LE_ERROR("failed to get file size: %s", curl_easy_strerror(rc));
        return -1;
    }

    memset(pkgInfoPtr->curlVersion, 0, BUF_SIZE);
    memcpy(pkgInfoPtr->curlVersion, curl_version(), BUF_SIZE);

    return 0;
}

//--------------------------------------------------------------------------------------------------
/**
 * InitDownload callback function definition
 */
//--------------------------------------------------------------------------------------------------
lwm2mcore_DwlResult_t pkgDwlCb_InitDownload
(
    char* uriPtr,
    void* ctxPtr
)
{
    static Package_t pkg;
    CURLcode rc;
    packageDownloader_DownloadCtx_t* dwlCtxPtr;

    dwlCtxPtr = (packageDownloader_DownloadCtx_t*)ctxPtr;

    pkg.curlPtr = NULL;

    dwlCtxPtr->ctxPtr = (void *)&pkg;

    LE_DEBUG("Initialize package downloader");

    // Check if download is not already aborted by an error during the Store thread initialization
    if (true == packageDownloader_CurrentDownloadToAbort())
    {
        return DWL_FAULT;
    }

    // initialize everything possible
    rc = curl_global_init(CURL_GLOBAL_ALL);
    if (CURLE_OK != rc)
    {
        LE_ERROR("failed to initialize libcurl: %s", curl_easy_strerror(rc));
        return DWL_FAULT;
    }

    // init the curl session
    pkg.curlPtr = curl_easy_init();
    if (!pkg.curlPtr)
    {
        LE_ERROR("failed to initialize the curl session");
        return DWL_FAULT;
    }

    // set URL to get here
    rc= curl_easy_setopt(pkg.curlPtr, CURLOPT_URL, uriPtr);
    if (CURLE_OK != rc)
    {
        LE_ERROR("failed to set URI: %s", curl_easy_strerror(rc));
        return DWL_FAULT;
    }

    // set the path to CA bundle
    rc= curl_easy_setopt(pkg.curlPtr, CURLOPT_CAINFO, dwlCtxPtr->certPtr);
    if (CURLE_OK != rc)
    {
        LE_ERROR("failed to set CA path: %s", curl_easy_strerror(rc));
        return DWL_FAULT;
    }

    if (-1 == GetDownloadInfo(&pkg))
    {
        return DWL_FAULT;
    }

    if (-1 == CheckHttpStatusCode(pkg.pkgInfo.httpRespCode))
    {
        return DWL_FAULT;
    }

    pkg.uriPtr = uriPtr;

    return DWL_OK;
}

//--------------------------------------------------------------------------------------------------
/**
 * GetInfo callback function definition
 */
//--------------------------------------------------------------------------------------------------
lwm2mcore_DwlResult_t pkgDwlCb_GetInfo
(
    lwm2mcore_PackageDownloaderData_t* dataPtr,
    void*                              ctxPtr
)
{
    packageDownloader_DownloadCtx_t* dwlCtxPtr;
    Package_t* pkgPtr;
    PackageInfo_t* pkgInfoPtr;

    dwlCtxPtr = (packageDownloader_DownloadCtx_t*)ctxPtr;
    pkgPtr = (Package_t*)dwlCtxPtr->ctxPtr;
    pkgInfoPtr = &pkgPtr->pkgInfo;

    // Check if download is not already aborted by an error during the Store thread initialization
    if (true == packageDownloader_CurrentDownloadToAbort())
    {
        return DWL_FAULT;
    }

    LE_DEBUG("using: %s", pkgInfoPtr->curlVersion);
    LE_DEBUG("connection status: %ld", pkgInfoPtr->httpRespCode);
    LE_DEBUG("package full size: %g MiB", pkgInfoPtr->totalSize / MEBIBYTE);
    LE_DEBUG("updateType: %d", dataPtr->updateType);

    dataPtr->packageSize = (uint64_t)pkgInfoPtr->totalSize;
    if(LWM2MCORE_FW_UPDATE_TYPE == dataPtr->updateType)
    {
        packageDownloader_SetFwUpdatePackageSize(dataPtr->packageSize);
    }
    else
    {
        LE_ERROR("incorrect update type");
    }

    return DWL_OK;
}

//--------------------------------------------------------------------------------------------------
/**
 * SetFwUpdateState callback function definition
 */
//--------------------------------------------------------------------------------------------------
lwm2mcore_DwlResult_t pkgDwlCb_SetFwUpdateState
(
    lwm2mcore_FwUpdateState_t updateState
)
{
    le_result_t result;

    result = packageDownloader_SetFwUpdateState(updateState);
    if (LE_OK != result)
    {
        return DWL_FAULT;
    }

    return DWL_OK;
}

//--------------------------------------------------------------------------------------------------
/**
 * SetFwUpdateResult callback function definition
 */
//--------------------------------------------------------------------------------------------------
lwm2mcore_DwlResult_t pkgDwlCb_SetFwUpdateResult
(
    lwm2mcore_FwUpdateResult_t updateResult
)
{
    le_result_t result;

    result = packageDownloader_SetFwUpdateResult(updateResult);
    if (LE_OK != result)
    {
        return DWL_FAULT;
    }

    return DWL_OK;
}

//--------------------------------------------------------------------------------------------------
/**
 * Download user agreement callback function definition
 */
//--------------------------------------------------------------------------------------------------
lwm2mcore_DwlResult_t pkgDwlCb_UserAgreement
(
    uint32_t pkgSize        ///< Package size
)
{
    le_result_t result;
    lwm2mcore_DwlResult_t dwlResult = DWL_OK;

    // Check if download is not already aborted by an error during the Store thread initialization
    if (true == packageDownloader_CurrentDownloadToAbort())
    {
        return DWL_FAULT;
    }

    result = avcServer_QueryDownload(lwm2mcore_PackageDownloaderAcceptDownload, pkgSize);

    // Get user agreement before starting package download
    if (LE_FAULT == result)
    {
        LE_ERROR("Unexpected error in Query Download.");
        dwlResult = DWL_FAULT;
    }
    else if (LE_OK == result)
    {
        LE_DEBUG("Download accepted");
        lwm2mcore_PackageDownloaderAcceptDownload();
    }
    else
    {
        LE_DEBUG("Download deffered");
    }

    return dwlResult;
}

//--------------------------------------------------------------------------------------------------
/**
 * Download callback function definition
 */
//--------------------------------------------------------------------------------------------------
lwm2mcore_DwlResult_t pkgDwlCb_Download
(
    uint64_t    startOffset,
    void*       ctxPtr
)
{
    packageDownloader_DownloadCtx_t* dwlCtxPtr;
    Package_t* pkgPtr;
    CURLcode rc;

    dwlCtxPtr = (packageDownloader_DownloadCtx_t*)ctxPtr;
    pkgPtr = (Package_t*)dwlCtxPtr->ctxPtr;

    curl_easy_setopt(pkgPtr->curlPtr, CURLOPT_NOBODY, 0L);
    curl_easy_setopt(pkgPtr->curlPtr, CURLOPT_WRITEFUNCTION, Write);

    // Start download at offset given by startOffset
    if (startOffset)
    {
        char buf[BUF_SIZE];
        snprintf(buf, BUF_SIZE, "%llu-", (unsigned long long)startOffset);
        curl_easy_setopt(pkgPtr->curlPtr, CURLOPT_RANGE, buf);
    }

    rc = curl_easy_perform(pkgPtr->curlPtr);

    if (true == packageDownloader_CurrentDownloadToAbort())
    {
        // Download is aborted: stop the parser by returning a download error
        return DWL_FAULT;
    }

    if (true == packageDownloader_CheckDownloadToSuspend())
    {
        // Download is suspended
        return DWL_OK;
    }

    if (   (CURLE_OK != rc)
        && (CURLE_WRITE_ERROR != rc)    // Expected when parsing aborted the download
       )
    {
        LE_ERROR("curl_easy_perform failed: %s", curl_easy_strerror(rc));
        return DWL_FAULT;
    }

    return DWL_OK;
}

//--------------------------------------------------------------------------------------------------
/**
 * StoreRange callback function definition
 */
//--------------------------------------------------------------------------------------------------
lwm2mcore_DwlResult_t pkgDwlCb_StoreRange
(
    uint8_t* bufPtr,
    size_t   bufSize,
    void*    ctxPtr
)
{
    packageDownloader_DownloadCtx_t* dwlCtxPtr;
    ssize_t count;

    dwlCtxPtr = (packageDownloader_DownloadCtx_t*)ctxPtr;

    count = write(dwlCtxPtr->downloadFd, bufPtr, bufSize);

    if (-1 == count)
    {
        LE_ERROR("failed to write to fifo: %m");
        return DWL_FAULT;
    }

    if (bufSize > count)
    {
        LE_ERROR("failed to write data: size %zu, count %zd", bufSize, count);
        return DWL_FAULT;
    }

    return DWL_OK;
}

//--------------------------------------------------------------------------------------------------
/**
 * EndDownload callback function definition
 */
//--------------------------------------------------------------------------------------------------
lwm2mcore_DwlResult_t pkgDwlCb_EndDownload
(
    void* ctxPtr
)
{
    packageDownloader_DownloadCtx_t* dwlCtxPtr;
    Package_t* pkgPtr;

    dwlCtxPtr = (packageDownloader_DownloadCtx_t*)ctxPtr;
    pkgPtr = (Package_t*)dwlCtxPtr->ctxPtr;

    curl_easy_cleanup(pkgPtr->curlPtr);

    curl_global_cleanup();

    return DWL_OK;
}
