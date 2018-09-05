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

#ifndef _AAMP_AES_H_
#define _AAMP_AES_H_

#include <stddef.h> // for size_t
#include "HlsDrmBase.h"
#include "drm.h"
#include "openssl/evp.h"

/**
 * @class AesDec
 * @brief Vanilla AES based DRM management
 */
class AesDec : public HlsDrmBase
{
public:
	static AesDec* GetInstance();
	int SetContext( PrivateInstanceAAMP *aamp, void* metadata, const DrmInfo *drmInfo);
	DrmReturn Decrypt(ProfilerBucketType bucketType, void *encryptedDataPtr, size_t encryptedDataLen, int timeInMs);
	void Release();
	void CancelKeyWait();
	void RestoreKeyState();

	/*Functions to support internal operations*/
	void AcquireKey();
	void SignalKeyAcquired();
	void NotifyDRMError(AAMPTuneFailure drmFailure);
	void SignalDrmError();
	void WaitForKeyAcquireCompleteUnlocked(int timeInMs, DrmReturn &err);

private:

	AesDec();
	~AesDec();
	static AesDec *mInstance;
	PrivateInstanceAAMP *mpAamp;
	pthread_cond_t mCond;
	pthread_mutex_t mMutex;
	EVP_CIPHER_CTX mOpensslCtx;
	DrmInfo mDrmInfo ;
	GrowableBuffer mAesKeyBuf;
	DRMState mDrmState;
	DRMState mPrevDrmState;
	char* mDrmUrl;
	int mCurlInstance;
};

#endif // _AAMP_AES_H_
