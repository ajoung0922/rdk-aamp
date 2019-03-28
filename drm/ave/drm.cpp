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
 * @file drm.cpp
 * @brief AVE DRM helper definitions
 */


#if 0
per Doug Adler, following hack(forcing true) allows initialization to complete
rdk\generic\ave\third_party\drm-public\portingkit\robust\CConstraintEnforcer.cpp

IConstraintEnforcer::Status OutputProtectionEnforcer::isConstraintSatisfiedInner(const IOutputProtectionConstraint& opConstraint, bool validateResult)
{
	ACBool result = true;
	ACUns32 error = 0;
	logprintf("%s --> %d\n", __FUNCTION__, __LINE__);
#if 0 // hack
	...
#endif
	return std::pair<ACBool, ACUns32>(result, error);
#endif

// TODO: THIS MODULE NEEDS TO BE MADE MULTI-SESSION-FRIENDLY
#include "drm.h"
#ifdef AVE_DRM
#include "media/IMedia.h" // TBR - remove dependency
#include <sys/time.h>
#include <stdio.h> // for printf
#include <stdlib.h> // for malloc
#include <pthread.h>
#include <errno.h>

using namespace media;

#define DRM_ERROR_SENTINEL_FILE "/tmp/DRM_Error"

#endif /*!NO_AVE_DRM*/

static pthread_mutex_t aveDrmManagerMutex = PTHREAD_MUTEX_INITIALIZER;

#ifdef AVE_DRM

static int drmSignalKeyAquired(void * arg);
static int drmSignalError(void * arg);
static pthread_mutex_t aveDrmManagerMutex = PTHREAD_MUTEX_INITIALIZER;

/*
From FlashAccessKeyFormats.pdf:
- DRM Key: RSA 1024 bit key pair generated by OEM; public key passed to Adobe
- Machine Key: RSA 1024 bit key pair generated by Adobe Individualization server
- Individualization Transport Key: RSA 1024 bit public key
- Individualization Response Wrapper Key: ARS 128 bit key
- Machine Transport Key: RSA 1024 bit key pair generated by Adobe individualization server
- Session Keys
- Content Keys: AES 128 bit keys

1. Individualization
- done first time device attempts to access protected content
- device and player/runtime identifiers sent to Adobe-hosted server
- each device is assigned machine private key
- Adobe server returns a unique digital machine certificate
- certificate can be revoked to prevent future license acquisition
- persists across reboots as a .bin file

2. Key Negotiation
- client parses manifest, reading URL of local comcast license server (from EXT-X-KEY)
- client extracts initialization vector (IV); placeholder for stronger security (from EXT-X-KEY)
- client collects additional channel-specific DRM metadata from manifest (from EXT-X-FAXS-CM)
- client transmits DRM metadata and machine certificate as parameters to an encrypted license request
- license request is encrypted using Transport public key (from DRM metadata)
- license server returns Content Encryption Key (CEK) and usage rules
- license is signed using license server private key
- license response is signed using Transport private key, and encrypted before being returned to client
- usage rules may authorize offline access
- Flash Player or Adobe AIR runtime client extracts Content Encryption Key (CEK) from license, allowing consumer to view authorized content

3. Decoding/Playback
- Advanced Encryption Standard 128-bit key (AES-128) decryption
- PKCS7 padding
- video output protection enforcement
*/

/**
 * @class TheDRMListener
 * @brief
 */
class TheDRMListener : public MyDRMListener
{
private:
	PrivateInstanceAAMP *mpAamp;
	AveDrm* mpAveDrm;
public:

/**
 * @addtogroup AAMP_DRM_API
 * @{
 */
	/**
	 * @brief TheDRMListener Constructor
	 */
	TheDRMListener(PrivateInstanceAAMP *pAamp, AveDrm* aveDrm)
	{
		mpAamp = pAamp;
		mpAveDrm = aveDrm;
		logprintf("TheDRMListener::%s:%d[%p]->[%p]\n", __FUNCTION__, __LINE__, mpAveDrm,this);
	}

	/**
	 * @brief TheDRMListener Constructor
	 */
	~TheDRMListener()
	{
	}

	/**
	 * @brief Callback on key acquired
	 */
	void SignalKeyAcquired()
	{
		logprintf("DRMListener::%s:%d[%p]drmState:%d moving to KeyAcquired\n", __FUNCTION__, __LINE__, mpAveDrm, mpAveDrm->mDrmState);
		mpAveDrm->SetState(eDRM_KEY_ACQUIRED);
		mpAamp->LogDrmInitComplete();
	}

	/**
	 * @brief Callback on initialization success
	 */
	void NotifyInitSuccess()
	{ // callback from successful pDrmAdapter->Initialize
		//log_current_time("NotifyInitSuccess\n");
		PrivateInstanceAAMP::AddIdleTask(drmSignalKeyAquired, this);
	}

	/**
	 * @brief Callback on drm error
	 */
	void NotifyDRMError(uint32_t majorError, uint32_t minorError)//(ErrorCode majorError, DRMMinorError minorError, AVString* errorString, media::DRMMetadata* metadata)
	{
		AAMPTuneFailure drmFailure = AAMP_TUNE_UNTRACKED_DRM_ERROR;
		char *description = NULL;
		bool isRetryEnabled = true;

		switch(majorError)
		{
		case 3329:
			/* Disable retry for error codes above 12000 which are listed as 
			 * Enitilement restriction error codes from license server.
			 * 12000 is recovering on retry so kept retry logic for that.
			 * Exclude the known 4xx errors also from retry, not giving
			 * all 4xx errors since 429 was found to be recovering on retry
			*/
			if(minorError > 12000 || (minorError >= 400 && minorError <= 412))
			{
				isRetryEnabled = false;
			}
			if(12012 == minorError || 12013 == minorError)
			{
				drmFailure = AAMP_TUNE_AUTHORISATION_FAILURE;
			}
			break;

		case 3321:
		case 3322:
		case 3328:
                        drmFailure = AAMP_TUNE_CORRUPT_DRM_DATA;

                        description = new char[MAX_ERROR_DESCRIPTION_LENGTH];

                        /*
                        * Creating file "/tmp/DRM_Error" will invoke self heal logic in
                        * ASCPDriver.cpp and regenrate cert files in /opt/persistent/adobe
                        * in the next tune attempt, this could clear tune error scenarios
                        * due to corrupt drm data.
                        */
                        FILE *sentinelFile;
                        sentinelFile = fopen(DRM_ERROR_SENTINEL_FILE,"w");

                        if(sentinelFile)
                        {
                                fclose(sentinelFile);
                        }
                        else
                        {
                        logprintf("DRMListener::%s:%d[%p] : *** /tmp/DRM_Error file creation for self heal failed. Error %d -> %s\n",
                                                 __FUNCTION__, __LINE__, mpAveDrm, errno, strerror(errno));
                        }

                        snprintf(description, MAX_ERROR_DESCRIPTION_LENGTH - 1, "AAMP: DRM Failure possibly due to corrupt drm data; majorError = %d, minorError = %d",(int)majorError, (int)minorError);
                        break;

                 case 3307:
                        drmFailure = AAMP_TUNE_DEVICE_NOT_PROVISIONED;
                        isRetryEnabled = false;
                        break;

		default:
                    break;
		}

		mpAamp->DisableDownloads();
		

		if(AAMP_TUNE_UNTRACKED_DRM_ERROR == drmFailure)
		{
			description = new char[MAX_ERROR_DESCRIPTION_LENGTH];
			snprintf(description, MAX_ERROR_DESCRIPTION_LENGTH - 1, "AAMP: DRM Failure majorError = %d, minorError = %d",(int)majorError, (int)minorError);
		}

		mpAamp->SendErrorEvent(drmFailure, description, isRetryEnabled);

		if (description)
		{
			delete[] description;
		}
	
		PrivateInstanceAAMP::AddIdleTask(drmSignalError, this);
		logprintf("DRMListener::%s:%d[%p] majorError = %d, minorError = %d drmState:%d\n", __FUNCTION__, __LINE__, mpAveDrm, (int)majorError, (int)minorError, mpAveDrm->mDrmState );

		AAMP_LOG_DRM_ERROR ((int)majorError, (int)minorError);
	}



	/**
	 * @brief Signal drm error
	 */
	void SignalDrmError()
	{
		mpAveDrm->SetState(eDRM_KEY_FAILED);
	}



	/**
	 * @brief Callback on drm status change
	 */
	void NotifyDRMStatus()//media::DRMMetadata* metadata, const DRMLicenseInfo* licenseInfo)
	{ // license available
		logprintf("DRMListener::%s:%d[%p]aamp:***drmState:%d\n", __FUNCTION__, __LINE__, mpAveDrm, mpAveDrm->mDrmState);
	}
};

namespace FlashAccess {

	/**
	 * @brief Caches drm resources
	 */
	void CacheDRMResources();
}


/**
 * @brief Signal key acquired to listener
 *
 * @param[in] arg drm status listener
 *
 * @retval 0
 */
static int drmSignalKeyAquired(void * arg)
{
	TheDRMListener * drmListener = (TheDRMListener*)arg;
	drmListener->SignalKeyAcquired();
	return 0;
}


/**
 * @brief Signal drm error to listener
 *
 * @param[in] arg drm status listener
 *
 * @retval 0
 */
static int drmSignalError(void * arg)
{
	TheDRMListener * drmListener = (TheDRMListener*)arg;
	drmListener->SignalDrmError();
	return 0;
}

/**
 * @brief prepare for decryption - individualization & license acquisition
 *
 * @param[in] aamp      Pointer to PrivateInstanceAAMP object associated with player
 * @param[in] metadata  Pointed to DrmMetadata structure - unpacked binary metadata from EXT-X-FAXS-CM
 *
 * @retval eDRM_SUCCESS on success
 */
DrmReturn AveDrm::SetMetaData( class PrivateInstanceAAMP *aamp, void *metadata)
{
	const DrmMetadata *drmMetadata = ( DrmMetadata *)metadata;
	pthread_mutex_lock(&mutex);
	mpAamp = aamp;
	DrmReturn err = eDRM_ERROR;
	if( !m_pDrmAdapter )
	{
		m_pDrmAdapter = new MyFlashAccessAdapter();
		m_pDrmListner = new TheDRMListener(mpAamp, this);
	}

	if (m_pDrmAdapter)
	{
		mDrmState = eDRM_ACQUIRING_KEY;
		m_pDrmAdapter->Initialize( drmMetadata, m_pDrmListner );
		m_pDrmAdapter->SetupDecryptionContext();
		err = eDRM_SUCCESS;
	}
	else
	{
		mDrmState = eDRM_INITIALIZED;
	}
	mPrevDrmState = eDRM_INITIALIZED;
	pthread_mutex_unlock(&mutex);
	logprintf("AveDrm::%s:%d[%p] drmState:%d \n", __FUNCTION__, __LINE__, this, mDrmState);
	return err;
}

/**
 * @brief Set information required for decryption
 *
 * @param[in] aamp     AAMP instance to be associated with this decryptor
 * @param[in] drmInfo  DRM information required to decrypt
 *
 * @retval eDRM_SUCCESS on success
 */
DrmReturn AveDrm::SetDecryptInfo( PrivateInstanceAAMP *aamp, const DrmInfo *drmInfo)
{
	DrmReturn err = eDRM_ERROR;
	pthread_mutex_lock(&mutex);
	mpAamp = aamp;

	if (m_pDrmAdapter)
	{
		m_pDrmAdapter->SetDecryptInfo(drmInfo);
		err = eDRM_SUCCESS;
	}
	else
	{
		logprintf("AveDrm::%s:%d[%p] ERROR -  NULL m_pDrmAdapter\n", __FUNCTION__, __LINE__, this);
	}
	pthread_mutex_unlock(&mutex);
	return eDRM_SUCCESS;
}

/**
 * @brief Decrypts an encrypted buffer
 *
 * @param[in] bucketType        Type of bucket for profiling
 * @param[in] encryptedDataPtr  Pointer to encyrpted payload
 * @param[in] encryptedDataLen  Length in bytes of data pointed to by encryptedDataPtr
 * @param[in] timeInMs          Wait time
 */
DrmReturn AveDrm::Decrypt( ProfilerBucketType bucketType, void *encryptedDataPtr, size_t encryptedDataLen,int timeInMs)
{
	DrmReturn err = eDRM_ERROR;

	pthread_mutex_lock(&mutex);
	if (mDrmState == eDRM_ACQUIRING_KEY )
	{
		logprintf( "AveDrm::%s:%d[%p] waiting for key acquisition to complete,wait time:%d\n", __FUNCTION__, __LINE__, this, timeInMs );
		struct timespec ts;
		struct timeval tv;
		gettimeofday(&tv, NULL);
		ts.tv_sec = time(NULL) + timeInMs / 1000;
		ts.tv_nsec = (long)(tv.tv_usec * 1000 + 1000 * 1000 * (timeInMs % 1000));
		ts.tv_sec += ts.tv_nsec / (1000 * 1000 * 1000);
		ts.tv_nsec %= (1000 * 1000 * 1000);

		if(ETIMEDOUT == pthread_cond_timedwait(&cond, &mutex, &ts)) // block until drm ready
		{
			logprintf("AveDrm::%s:%d[%p] wait for key acquisition timed out\n", __FUNCTION__, __LINE__, this);
			err = eDRM_KEY_ACQUSITION_TIMEOUT;
		}
	}

	if (mDrmState == eDRM_KEY_ACQUIRED)
	{
		// create same-sized buffer for decrypted data; q: can we decrypt in-place?
		struct DRMBuffer decryptedData;
		decryptedData.buf = (uint8_t *)malloc(encryptedDataLen);
		if (decryptedData.buf)
		{
			decryptedData.len = (uint32_t)encryptedDataLen;

			struct DRMBuffer encryptedData;
			encryptedData.buf = (uint8_t *)encryptedDataPtr;
			encryptedData.len = (uint32_t)encryptedDataLen;

			mpAamp->LogDrmDecryptBegin(bucketType);
			if( 0 == m_pDrmAdapter->Decrypt(true, encryptedData, decryptedData))
			{
				err = eDRM_SUCCESS;
			}
			mpAamp->LogDrmDecryptEnd(bucketType);

			memcpy(encryptedDataPtr, decryptedData.buf, encryptedDataLen);
			free(decryptedData.buf);
		}
	}
	else
	{
		logprintf( "AveDrm::%s:%d[%p]  aamp:key acquisition failure! mDrmState = %d\n", __FUNCTION__, __LINE__, this, (int)mDrmState);
	}
	pthread_mutex_unlock(&mutex);
	return err;
}


/**
 * @brief Release drm session
 */
void AveDrm::Release()
{
	pthread_mutex_lock(&mutex);
	if (m_pDrmAdapter)
	{
		// close all drm sessions
		m_pDrmAdapter->AbortOperations();
	}
	pthread_cond_broadcast(&cond);
	pthread_mutex_unlock(&mutex);
}


/**
 * @brief Cancel timed_wait operation drm_Decrypt
 */
void AveDrm::CancelKeyWait()
{
	pthread_mutex_lock(&mutex);
	//save the current state in case required to restore later.
	mPrevDrmState = mDrmState;
	//required for demuxed assets where the other track might be waiting on mutex lock.
	mDrmState = eDRM_KEY_FLUSH;
	pthread_cond_broadcast(&cond);
	pthread_mutex_unlock(&mutex);
}


/**
 * @brief Restore key state post cleanup of audio/video TrackState in case DRM data is persisted
 */
void AveDrm::RestoreKeyState()
{
	pthread_mutex_lock(&mutex);
	//In case somebody overwritten mDrmState before restore operation, keep that state
	if (mDrmState == eDRM_KEY_FLUSH)
	{
		mDrmState = mPrevDrmState;
	}
	pthread_mutex_unlock(&mutex);
}
#else

DrmReturn AveDrm::SetMetaData(class PrivateInstanceAAMP *aamp, void *drmMetadata)
{
	return eDRM_ERROR;
}


DrmReturn AveDrm::SetDecryptInfo( PrivateInstanceAAMP *aamp, const DrmInfo *drmInfo)
{
	return eDRM_ERROR;
}

DrmReturn AveDrm::Decrypt( ProfilerBucketType bucketType, void *encryptedDataPtr, size_t encryptedDataLen,int timeInMs)
{
	return eDRM_SUCCESS;
}


void AveDrm::Release()
{
}


void AveDrm::CancelKeyWait()
{
}

void AveDrm::RestoreKeyState()
{
}
#endif // !AVE_DRM


/**
 * @brief AveDrm Constructor
 */
AveDrm::AveDrm() : mpAamp(NULL), m_pDrmAdapter(NULL), m_pDrmListner(NULL),
		mDrmState(eDRM_INITIALIZED), mPrevDrmState(eDRM_INITIALIZED)
{
	pthread_mutex_init(&mutex, NULL);
	pthread_cond_init(&cond, NULL);
}


AveDrm::~AveDrm()
{
	if(m_pDrmListner)
	{
		delete m_pDrmListner;
		m_pDrmListner = NULL;
	}	
	if(m_pDrmAdapter)
	{
		delete m_pDrmAdapter;
		m_pDrmAdapter = NULL;
	}
	pthread_mutex_destroy(&mutex);
	pthread_cond_destroy(&cond);
}


/**
 * @brief Set state and signal waiting threads. Used internally by listener.
 *
 * @param[in] state State to be set
 */
void AveDrm::SetState(DRMState state)
{
	pthread_mutex_lock(&mutex);
	mDrmState = state;
	pthread_cond_broadcast(&cond);
	pthread_mutex_unlock(&mutex);
}

/**
 * @brief AveDrmManager Constructor.
 *
 */
AveDrmManager::AveDrmManager() :
		mDrm(NULL)
{
	mDrm = std::make_shared<AveDrm>();
	Reset();
}


//#define ENABLE_AVE_DRM_MANGER_DEBUG
#ifdef ENABLE_AVE_DRM_MANGER_DEBUG
#define AVE_DRM_MANGER_DEBUG logprintf
#else
#define AVE_DRM_MANGER_DEBUG traceprintf
#endif

/**
 * @brief Reset state of AveDrmManager instance. Used internally
 */
void AveDrmManager::Reset()
{
	mDrmContexSet = false;
	memset(mSha1Hash, 0, DRM_SHA1_HASH_LEN);
	userCount = 0;
	trackType = 0;
}

void AveDrmManager::UpdateBeforeIndexList(const char* trackname,int trackType)
{
	pthread_mutex_lock(&aveDrmManagerMutex);
	for (int i = 0; i < sAveDrmManager.size(); i++)
        {
		if(sAveDrmManager[i]->trackType == trackType){
	                sAveDrmManager[i]->userCount--;
		}
        }
	pthread_mutex_unlock(&aveDrmManagerMutex);
}

void AveDrmManager::FlushAfterIndexList(const char* trackname,int trackType)
{
	std::vector<AveDrmManager*>::iterator iter;
	pthread_mutex_lock(&aveDrmManagerMutex);
	for (iter = sAveDrmManager.begin(); iter != sAveDrmManager.end();)
	{
		AveDrmManager* aveDrmManager = *iter;
		if(aveDrmManager->trackType == trackType && aveDrmManager->userCount <= 0)
		{
			aveDrmManager->mDrm->Release();
			aveDrmManager->Reset();
			delete aveDrmManager;
			iter = sAveDrmManager.erase(iter);
			logprintf("[%s][%s] Erased unused DRM Metadata.Size remaining=%d \n",__FUNCTION__,trackname,sAveDrmManager.size());
		}
		else
		{
			iter++;
		}

	}
	pthread_mutex_unlock(&aveDrmManagerMutex);
}


/**
 * @brief Reset state of AveDrmManager.
 */
void AveDrmManager::ResetAll()
{
	pthread_mutex_lock(&aveDrmManagerMutex);
	for (int i = 0; i < sAveDrmManager.size(); i++)
	{
		sAveDrmManager[i]->Reset();
	}
	pthread_mutex_unlock(&aveDrmManagerMutex);
}

/**
 * @brief Cancel wait inside Decrypt function of all active DRM instances
 */
void AveDrmManager::CancelKeyWaitAll()
{
	pthread_mutex_lock(&aveDrmManagerMutex);
	for (int i = 0; i < sAveDrmManager.size(); i++)
	{
		sAveDrmManager[i]->mDrm->CancelKeyWait();
	}
	pthread_mutex_unlock(&aveDrmManagerMutex);
}

/**
 * @brief Release all active DRM instances
 */
void AveDrmManager::ReleaseAll()
{
	// Only called from Stop of fragment collector of hls , mutex protection 
	// added for calling 
	
	logprintf("[%s]Releasing AveDrmManager of size=%d \n",__FUNCTION__,sAveDrmManager.size());
	std::vector<AveDrmManager*>::iterator iter;
	pthread_mutex_lock(&aveDrmManagerMutex);
	for (iter = sAveDrmManager.begin(); iter != sAveDrmManager.end();)
	{
		AveDrmManager* aveDrmManager = *iter;
		aveDrmManager->mDrm->Release();
		aveDrmManager->Reset();
		delete aveDrmManager;
		iter = sAveDrmManager.erase(iter);
	}
	pthread_mutex_unlock(&aveDrmManagerMutex);
}

/**
 * @brief Restore key states of all active DRM instances
 */
void AveDrmManager::RestoreKeyStateAll()
{
	pthread_mutex_lock(&aveDrmManagerMutex);
	for (int i = 0; i < sAveDrmManager.size(); i++)
	{
		sAveDrmManager[i]->mDrm->RestoreKeyState();
	}
	pthread_mutex_unlock(&aveDrmManagerMutex);
}

/**
 * @brief Set DRM meta-data. Creates AveDrm instance if meta-data is not already configured.
 *
 * @param[in] aamp          AAMP instance associated with the operation.
 * @param[in] metaDataNode  DRM meta data node containing meta-data to be set.
 */
void AveDrmManager::SetMetadata(PrivateInstanceAAMP *aamp, DrmMetadataNode *metaDataNode,int trackType)
{
	AveDrmManager* aveDrmManager = NULL;
	bool drmMetaDataSet = false;
	AVE_DRM_MANGER_DEBUG ("%s:%d: Enter sAveDrmManager.size = %d\n", __FUNCTION__, __LINE__, (int)sAveDrmManager.size());
	pthread_mutex_lock(&aveDrmManagerMutex);
	for (int i = 0; i < sAveDrmManager.size(); i++)
	{
		if (sAveDrmManager[i]->mDrmContexSet)
		{
			if (0 == memcmp(metaDataNode->sha1Hash, sAveDrmManager[i]->mSha1Hash, DRM_SHA1_HASH_LEN))
			{
				sAveDrmManager[i]->userCount++;
				AVE_DRM_MANGER_DEBUG ("%s:%d: Found matching sha1Hash. Index[%d] UserCount[%d]\n", __FUNCTION__, __LINE__, i,sAveDrmManager[i]->userCount);
				drmMetaDataSet = true;
				break;
			}
			else
			{
#ifdef ENABLE_AVE_DRM_MANGER_DEBUG
				printf("%s:%d sHlsDrmContext[%d].mSha1Hash -  ", __FUNCTION__, __LINE__, i);
				PrintSha1Hash(sAveDrmManager[i]->mSha1Hash);
				printf("metaDataNode->sha1Hash - ");
				PrintSha1Hash(metaDataNode->sha1Hash);
#endif
			}
		}
		else if (!aveDrmManager)
		{
			aveDrmManager = sAveDrmManager[i];
		}
	}
	if (!drmMetaDataSet)
	{
		if (!aveDrmManager)
		{
			logprintf("%s:%d: Create new AveDrmManager object\n", __FUNCTION__, __LINE__);
			aveDrmManager = new AveDrmManager();
			sAveDrmManager.push_back(aveDrmManager);
			if (sAveDrmManager.size() > MAX_DRM_CONTEXT)
			{
				logprintf("%s:%d: WARNING - %d AveDrmManager objects allocated\n", __FUNCTION__, __LINE__,sAveDrmManager.size());
			}
		}
		if (aveDrmManager)
		{
			aveDrmManager->mDrm->SetMetaData(aamp, &metaDataNode->metaData);
			aveDrmManager->mDrmContexSet = true;
			aveDrmManager->userCount++;
			aveDrmManager->trackType = trackType;
			memcpy(aveDrmManager->mSha1Hash, metaDataNode->sha1Hash, DRM_SHA1_HASH_LEN);
		}
	}
	pthread_mutex_unlock(&aveDrmManagerMutex);
	AVE_DRM_MANGER_DEBUG ("%s:%d: Exit sAveDrmManager.size = %d\n", __FUNCTION__, __LINE__, sAveDrmManager.size());
}

/**
 * @brief Print DRM metadata hash
 *
 * @param[in] sha1Hash SHA1 hash to be printed
 */
void AveDrmManager::PrintSha1Hash(char* sha1Hash)
{
	for (int j = 0; j < DRM_SHA1_HASH_LEN; j++)
	{
		printf("%c", sha1Hash[j]);
	}
	printf("\n");
}

/**
 * @brief Dump cached DRM licenses
 */
void AveDrmManager::DumpCachedLicenses()
{
	std::shared_ptr<AveDrm>  aveDrm = nullptr;
	printf("%s:%d sAveDrmManager.size %d\n", __FUNCTION__, __LINE__, (int)sAveDrmManager.size());
	for (int i = 0; i < sAveDrmManager.size(); i++)
	{
		printf("%s:%d sAveDrmManager[%d] mDrmContexSet %s .mSha1Hash -  ", __FUNCTION__, __LINE__, i,
				sAveDrmManager[i]->mDrmContexSet?"true":"false");
		PrintSha1Hash(sAveDrmManager[i]->mSha1Hash);
	}
	printf("\n");
}


/**
 * @brief Get AveDrm instance configured with a specific metadata
 *
 * @param[in] sha1Hash SHA1 hash of meta-data
 *
 * @return AveDrm  Instance corresponds to sha1Hash
 * @return NULL    If AveDrm instance configured with the meta-data is not available
 */
std::shared_ptr<AveDrm> AveDrmManager::GetAveDrm(char* sha1Hash)
{
	std::shared_ptr<AveDrm>  aveDrm = nullptr;
#ifdef ENABLE_AVE_DRM_MANGER_DEBUG
	printf("%s:%d Enter sAveDrmManager.size = %d sha1Hash -  ", __FUNCTION__, __LINE__, (int)sAveDrmManager.size());
	PrintSha1Hash(sha1Hash);
#endif
	pthread_mutex_lock(&aveDrmManagerMutex);
	for (int i = 0; i < sAveDrmManager.size(); i++)
	{
		if (sAveDrmManager[i]->mDrmContexSet)
		{
			if (0 == memcmp(sha1Hash, sAveDrmManager[i]->mSha1Hash, DRM_SHA1_HASH_LEN))
			{
				aveDrm = sAveDrmManager[i]->mDrm;
				AVE_DRM_MANGER_DEBUG ("%s:%d: Found matching sha1Hash. Index[%d] aveDrm[%p]\n", __FUNCTION__, __LINE__, i, aveDrm);
				break;
			}
			else
			{
#ifdef ENABLE_AVE_DRM_MANGER_DEBUG
				printf("%s:%d sHlsDrmContext[%d].mSha1Hash -  ", __FUNCTION__, __LINE__, i);
				PrintSha1Hash(sAveDrmManager[i]->mSha1Hash);
#endif
			}
		}
		else
		{
			logprintf("%s:%d: sHlsDrmContext[%d].mDrmContexSet is false\n", __FUNCTION__, __LINE__, i);
		}
	}
	pthread_mutex_unlock(&aveDrmManagerMutex);
	return aveDrm;
}

/**
 * @brief Get index of drm meta-data which is not yet configured
 *
 * @param[in] drmMetadataIdx   Indexed DRM meta-data
 * @param[in] drmMetadataCount Count of meta-data present in the index
 */
int AveDrmManager::GetNewMetadataIndex(DrmMetadataNode* drmMetadataIdx, int drmMetadataCount)
{
	int idx = -1;
	pthread_mutex_lock(&aveDrmManagerMutex);
	for (int j = drmMetadataCount - 1; j >= 0; j--)
	{
		bool matched = false;
		for (int i = 0; i < sAveDrmManager.size(); i++)
		{
			if (0 == memcmp(drmMetadataIdx[j].sha1Hash, sAveDrmManager[i]->mSha1Hash, DRM_SHA1_HASH_LEN))
			{
				matched = true;
				break;
			}
		}
		if (!matched)
		{
			idx = j;
			break;
		}
	}
	pthread_mutex_unlock(&aveDrmManagerMutex);
	return idx;
}

std::vector<AveDrmManager*> AveDrmManager::sAveDrmManager;


/**
 * @}
 */
