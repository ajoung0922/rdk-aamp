/*
 * If not stated otherwise in this file or this component's license file the
 * following copyright and licenses apply:
 *
 * Copyright 2018 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
*/


/**
* @file AampDRMutils.h
* @brief Data structures to help with DRM sessions.
*/

#ifndef AampDRMutils_h
#define AampDRMutils_h

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "AampDrmMediaFormat.h"
#include "AampDrmData.h"
#include "AampDrmInfo.h"
#include "AampDrmSystems.h"

/**
 * @brief Macros to track the value of API success or failure
 */
#define DRM_API_SUCCESS (0)
#define DRM_API_FAILED  (-1)

/**
 * @brief start and end tags for Comcast DRM policy
 */
#define COMCAST_DRM_METADATA_TAG_START "<ckm:policy xmlns:ckm=\"urn:ccp:ckm\">"
#define COMCAST_DRM_METADATA_TAG_END "</ckm:policy>"

void aamp_ConvertEndianness(unsigned char *original, unsigned char *guidBytes);

char *aamp_Base64_URL_Encode(const unsigned char *src, size_t len);

unsigned char *aamp_Base64_URL_Decode(const char *src, size_t *len, size_t srcLen);

unsigned char *aamp_ExtractDataFromPssh(const char* psshData, int dataLength,
										const char* startStr, const char* endStr, int *len);

std::string aamp_ExtractWVContentMetadataFromPssh(const char* psshData, int dataLength);

std::string aamp_getAbsoluteKeyUri(std::string manifestUrl, std::string keyUri);

char *aamp_Base64_URL_Encode(const unsigned char *src, size_t len);

unsigned char *aamp_Base64_URL_Decode(const char *src, size_t *len, size_t srcLen);

unsigned char *aamp_ExtractDataFromPssh(const char* psshData, int dataLength,
                                            const char* startStr, const char* endStr, int *len);

unsigned char * aamp_ExtractKeyIdFromPssh(const char* psshData, int dataLength, int *len, DRMSystems drmSystem);

unsigned char * aamp_ExtractWVContentMetadataFromPssh(const char* psshData, int dataLength, int *len);

#endif
