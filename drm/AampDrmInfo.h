/*
 * If not stated otherwise in this file or this component's license file the
 * following copyright and licenses apply:
 *
 * Copyright 2020 RDK Management
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

#ifndef AAMPDRMINFO_H
#define AAMPDRMINFO_H

#ifdef AVE_DRM
#include "ave-adapter/MyFlashAccessAdapter.h"
#else

#include <string>
#include "AampDrmMediaFormat.h"

/**
 * @struct DrmInfo
 * @brief DRM information required to decrypt
 */

/**
 * @enum DrmMethod
 * @brief DRM method
 */
typedef enum
{
	eMETHOD_NONE,
	eMETHOD_AES_128, /// encrypted using Advanced Encryption Standard 128-bit key and PKCS7 padding
} DrmMethod;

struct DrmInfo
{
	DrmInfo() : method(eMETHOD_NONE), mediaFormat(eMEDIAFORMAT_HLS), useFirst16BytesAsIV(false), iv(nullptr),
				masterManifestURL(), manifestURL(), keyURI(), keyFormat(), systemUUID(), initData()
	{};
	~DrmInfo() {};
	// copy constructor
	DrmInfo(const DrmInfo& other) : method(other.method), mediaFormat(other.mediaFormat),
					useFirst16BytesAsIV(other.useFirst16BytesAsIV), masterManifestURL(other.masterManifestURL),
					manifestURL(other.manifestURL), keyURI(other.keyURI), keyFormat(other.keyFormat),
					systemUUID(other.systemUUID), initData(other.initData), iv()
	{
		// copying same iv, releases memory allocated after deleting any of these objects.
		iv = other.iv;
	}
	DrmInfo& operator=(const DrmInfo& other)
	{
		method = other.method;
		mediaFormat = other.mediaFormat;
		useFirst16BytesAsIV = other.useFirst16BytesAsIV;
		masterManifestURL = other.masterManifestURL;
		manifestURL = other.manifestURL;
		keyURI = other.keyURI;
		keyFormat = other.keyFormat;
		systemUUID = other.systemUUID;
		initData = other.initData;
		// copying same iv, releases memory allocated after deleting any of these objects.
		iv = other.iv;
		return *this;
	}
	DrmMethod method;				// Encryption method
	MediaFormat mediaFormat;		// Format of the media being played e.g. DASH, HLS
	bool useFirst16BytesAsIV;
	unsigned char *iv;				// [16] Initialisation vector
	std::string masterManifestURL;	// URL of the master manifest
	std::string manifestURL;		// URL of playlist the DRM info was taken from. May be the same as the masterManifestURL
	std::string keyURI;				// URI to fetch key. May be relative to the manifest URL
	std::string keyFormat;			// Format of key
	std::string systemUUID;			// UUID of the DRM
	std::string initData;			// Base64 init data string from the main manifest URI
};


#endif /* AVE_DRM */
#endif /* AAMPDRMINFO_H */

