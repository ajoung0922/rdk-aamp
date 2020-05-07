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

#include "AampHlsOcdmBridge.h"

#define DRM_IV_LEN 16

using namespace std;

AampHlsOcdmBridge::AampHlsOcdmBridge(AampDrmSession * aampDrmSession) :
	m_drmInfo(nullptr),
	m_aampInstance(nullptr),
	m_drmSession(aampDrmSession),
	m_drmState(eDRM_INITIALIZED)
{
	pthread_mutex_init(&m_Mutex, NULL);
}

AampHlsOcdmBridge::~AampHlsOcdmBridge()
{
	pthread_mutex_destroy(&m_Mutex);
}

DrmReturn AampHlsOcdmBridge::SetDecryptInfo( PrivateInstanceAAMP *aamp, const struct DrmInfo *drmInfo)
{
	DrmReturn result  = eDRM_ERROR;

	pthread_mutex_lock(&m_Mutex);
	m_aampInstance = aamp;
	m_drmInfo = drmInfo;
	KeyState eKeyState = m_drmSession->getState();
	if (eKeyState == KEY_READY)
	{
		m_drmState = eDRM_KEY_ACQUIRED;
		result = eDRM_SUCCESS; //frag_collector ignores the return
	}
	pthread_mutex_unlock(&m_Mutex);
	AAMPLOG_TRACE("%s:%d DecryptInfo Set\n", __FUNCTION__, __LINE__);

	return result;
}

DrmReturn AampHlsOcdmBridge::Decrypt( ProfilerBucketType bucketType, void *encryptedDataPtr, size_t encryptedDataLen,int timeInMs)
{
	DrmReturn result = eDRM_ERROR;

	pthread_mutex_lock(&m_Mutex);
	if (m_drmState == eDRM_KEY_ACQUIRED)
	{
		 AAMPLOG_TRACE("%s:%d Starting decrypt\n", __FUNCTION__, __LINE__);
		 int retVal = m_drmSession->decrypt(m_drmInfo->iv, DRM_IV_LEN, (const uint8_t *)encryptedDataPtr , encryptedDataLen, NULL);
		 if (retVal)
		 {
			AAMPLOG_WARN("%s:%d: Decrypt failed err = %d\n", __FUNCTION__, __LINE__, retVal);
		 }
		 else
		 {
			AAMPLOG_TRACE("%s:%d Decrypt success\n", __FUNCTION__, __LINE__);
			result = eDRM_SUCCESS;
		 }
	}
	else
	{
		AAMPLOG_WARN("%s:%d:Decrypt Called in Incorrect State! DrmState = %d\n",  __FUNCTION__, __LINE__, (int)m_drmState);
	}
	pthread_mutex_unlock(&m_Mutex);

	return result;
}

void AampHlsOcdmBridge::Release(void)
{
	AAMPLOG_WARN("%s:%d: Releasing the Opencdm Session\n", __FUNCTION__, __LINE__);
	m_drmSession->clearDecryptContext();
}

void AampHlsOcdmBridge::CancelKeyWait(void)
{
	//TBD:Unimplemented
}

