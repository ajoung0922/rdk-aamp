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
 * @file admanager_mpd.cpp
 * @brief Client side DAI manger for MPEG DASH
 */

#include "admanager_mpd.h"
#include "fragmentcollector_mpd.h"

#include <algorithm>

static void *AdFulfillThreadEntry(void *arg)
{
    PrivateCDAIObjectMPD *_this = (PrivateCDAIObjectMPD *)arg;
    if(aamp_pthread_setname(pthread_self(), "AdFulfillThread"))
    {
        logprintf("%s:%d: aamp_pthread_setname failed\n", __FUNCTION__, __LINE__);
    }
    _this->FulFillAdObject();
    return NULL;
}


CDAIObjectMPD::CDAIObjectMPD(PrivateInstanceAAMP* aamp): CDAIObject(aamp), mPrivObj(new PrivateCDAIObjectMPD(aamp))
{

}

CDAIObjectMPD::~CDAIObjectMPD()
{
	delete mPrivObj;
}

/**
 * @}
 */
void CDAIObjectMPD::SetAlternateContents(const std::string &periodId, const std::string &adId, const std::string &url,  uint64_t startMS)
{
	mPrivObj->SetAlternateContents(periodId, adId, url, startMS);
}



PrivateCDAIObjectMPD::PrivateCDAIObjectMPD(PrivateInstanceAAMP* aamp) : mAamp(aamp),mDaiMtx(), mIsFogTSB(false), mAdBreaks(), mPeriodMap(), mCurPlayingBreakId(), mAdObjThreadID(0), mAdFailed(false), mCurAds(nullptr),
					mCurAdIdx(-1), mContentSeekOffset(0), mAdState(AdState::OUTSIDE_ADBREAK),mPlacementObj(), mAdFulfillObj()
{
	mAamp->CurlInit(AAMP_DAI_CURL_IDX, 1);
}

PrivateCDAIObjectMPD::~PrivateCDAIObjectMPD()
{
	if(mAdObjThreadID)
	{
		int rc = pthread_join(mAdObjThreadID, NULL);
		if (rc != 0)
		{
			logprintf("%s:%d ***pthread_join failed, returned %d\n", __FUNCTION__, __LINE__, rc);
		}
		mAdObjThreadID = 0;
	}
	mAamp->CurlTerm(AAMP_DAI_CURL_IDX, 1);
}

void PrivateCDAIObjectMPD::InsertToPeriodMap(IPeriod * period)
{
	const std::string &prdId = period->GetId();
	if(!isPeriodExist(prdId))
	{
		mPeriodMap[prdId] = Period2AdData();
	}
}

bool PrivateCDAIObjectMPD::isPeriodExist(const std::string &periodId)
{
	return (mPeriodMap.end() != mPeriodMap.find(periodId))?true:false;
}

inline bool PrivateCDAIObjectMPD::isAdBreakObjectExist(const std::string &adBrkId)
{
	return (mAdBreaks.end() != mAdBreaks.find(adBrkId))?true:false;
}


void PrivateCDAIObjectMPD::PrunePeriodMaps(std::vector<std::string> &newPeriodIds)
{
	//Erase all adbreaks other than new adbreaks
	for (auto it = mAdBreaks.begin(); it != mAdBreaks.end();) {
		if ((mPlacementObj.pendingAdbrkId != it->first) && (mCurPlayingBreakId != it->first) &&//We should not remove the pending/playing adbreakObj
				(newPeriodIds.end() == std::find(newPeriodIds.begin(), newPeriodIds.end(), it->first))) {
			auto &adBrkObj = *it;
			logprintf("%s:%d [CDAI] Removing the period[%s] from mAdBreaks.\n", __FUNCTION__, __LINE__, adBrkObj.first.c_str());
			auto adNodes = adBrkObj.second.ads;
			for(AdNode &ad: *adNodes)
			{
				if(ad.mpd)
				{
					delete ad.mpd;
				}
			}
			it = mAdBreaks.erase(it);
		} else {
			++it;
		}
	}

	//Erase all periods other than new periods
	for (auto it = mPeriodMap.begin(); it != mPeriodMap.end();) {
		if (newPeriodIds.end() == std::find(newPeriodIds.begin(), newPeriodIds.end(), it->first)) {
			it = mPeriodMap.erase(it);
		} else {
			++it;
		}
	}
}

void PrivateCDAIObjectMPD::ResetState()
{
	 //TODO: Vinod, maybe we can move these playback state variables to PrivateStreamAbstractionMPD
	 mIsFogTSB = false;
	 mCurPlayingBreakId = "";
	 mAdFailed = false;
	 mCurAds = nullptr;
	 mCurAdIdx = -1;
	 mContentSeekOffset = 0;
	 mAdState = AdState::OUTSIDE_ADBREAK;
}

void PrivateCDAIObjectMPD::ClearMaps()
{
	std::unordered_map<std::string, AdBreakObject> tmpMap;
	std::swap(mAdBreaks,tmpMap);
	for(auto &adBrkObj: tmpMap)
	{
		auto adNodes = adBrkObj.second.ads;
		for(AdNode &ad: *adNodes)
		{
			if(ad.mpd)
			{
				delete ad.mpd;
			}
		}
	}

	mPeriodMap.clear();
}

void  PrivateCDAIObjectMPD::PlaceAds(dash::mpd::IMPD *mpd)
{
	bool placed = false;
	//Populate the map to specify the period boundaries
	if(mpd && (-1 != mPlacementObj.curAdIdx) && "" != mPlacementObj.pendingAdbrkId && isAdBreakObjectExist(mPlacementObj.pendingAdbrkId)) //Some Ad is still waiting for the placement
	{
		bool openPrdFound = false;

		AdBreakObject &abObj = mAdBreaks[mPlacementObj.pendingAdbrkId];
		vector<IPeriod *> periods = mpd->GetPeriods();
		for(auto period: periods)
		{
			const std::string &periodId = period->GetId();
			//We need to check, open period is available in the manifest. Else, something wrong
			if(mPlacementObj.openPeriodId == periodId)
			{
				openPrdFound = true;
			}
			else if(openPrdFound)
			{
				if(aamp_GetPeriodDuration(period) > 0)
				{
					//Previous openPeriod ended. New period in the adbreak will be the new open period
					mPeriodMap[mPlacementObj.openPeriodId].filled = true;
					mPlacementObj.openPeriodId = periodId;
					mPlacementObj.curEndNumber = 0;
				}
				else
				{
					continue;		//Empty period may come early; excluding them
				}
			}

			if(openPrdFound && -1 != mPlacementObj.curAdIdx)
			{
				uint64_t periodDelta = aamp_GetPeriodNewContentDuration(period, mPlacementObj.curEndNumber);
				Period2AdData& p2AdData = mPeriodMap[periodId];

				if("" == p2AdData.adBreakId)
				{
					//New period opened
					p2AdData.adBreakId = mPlacementObj.pendingAdbrkId;
					p2AdData.offset2Ad[0] = AdOnPeriod{mPlacementObj.curAdIdx,mPlacementObj.adNextOffset};
				}

				p2AdData.duration += periodDelta;

				while(periodDelta > 0)
				{
					AdNode &curAd = abObj.ads->at(mPlacementObj.curAdIdx);
					if("" == curAd.basePeriodId)
					{
						//Next ad started placing
						curAd.basePeriodId = periodId;
						curAd.basePeriodOffset = p2AdData.duration - periodDelta;
						int offsetKey = curAd.basePeriodOffset;
						offsetKey = offsetKey - (offsetKey%OFFSET_ALIGN_FACTOR);
						p2AdData.offset2Ad[offsetKey] = AdOnPeriod{mPlacementObj.curAdIdx,0};	//At offsetKey of the period, new Ad starts
					}
					if(periodDelta < (curAd.duration - mPlacementObj.adNextOffset))
					{
						mPlacementObj.adNextOffset += periodDelta;
						periodDelta = 0;
					}
					else
					{
						//Current Ad completely placed. But more space available in the current period for next Ad
						curAd.placed = true;
						periodDelta -= (curAd.duration - mPlacementObj.adNextOffset);
						mPlacementObj.curAdIdx++;
						if(mPlacementObj.curAdIdx < abObj.ads->size())
						{
							mPlacementObj.adNextOffset = 0; //New Ad's offset
						}
						else
						{
							mPlacementObj.curAdIdx = -1;
							//Place the end markers of adbreak
							abObj.endPeriodId = periodId;	//If it is the exact period boundary, end period will be the next one
							abObj.endPeriodOffset = p2AdData.duration - periodDelta;
							if(abObj.endPeriodOffset < 5000)
							{
								abObj.endPeriodOffset = 0;//Aligning the last period
								mPeriodMap[abObj.endPeriodId] = Period2AdData(); //Resetting the period with small outlier.
							}
							//TODO: else We need to calculate duration of the end period in the Adbreak

							//Printing the placement positions
							std::stringstream ss;
							ss<<"{AdbreakId: "<<mPlacementObj.pendingAdbrkId;
							ss<<", duration: "<<abObj.duration;
							ss<<", endPeriodId: "<<abObj.endPeriodId;
							ss<<", endPeriodOffset: "<<abObj.endPeriodOffset;
							ss<<", #Ads: "<<abObj.ads->size() << ",[";
							for(int k=0;k<abObj.ads->size();k++)
							{
								AdNode &ad = abObj.ads->at(k);
								ss<<"\n{AdIdx:"<<k <<",AdId:"<<ad.adId<<",duration:"<<ad.duration<<",basePeriodId:"<<ad.basePeriodId<<", basePeriodOffset:"<<ad.basePeriodOffset<<"},";
							}
							ss<<"],\nUnderlyingPeriods:[ ";
							for(auto it = mPeriodMap.begin();it != mPeriodMap.end();it++)
							{
								if(it->second.adBreakId == mPlacementObj.pendingAdbrkId)
								{
									ss<<"\n{PeriodId:"<<it->first<<", duration:"<<it->second.duration;
									for(auto pit = it->second.offset2Ad.begin(); pit != it->second.offset2Ad.end() ;pit++)
									{
										ss<<", offset["<<pit->first<<"]=> Ad["<<pit->second.adIdx<<"@"<<pit->second.adStartOffset<<"]";
									}
								}
							}
							ss<<"]}";
							logprintf("%s:%d [CDAI] Placement Done: %s.\n", __FUNCTION__, __LINE__, ss.str().c_str());
							break;
						}
					}
				}
			}
		}
		if(-1 == mPlacementObj.curAdIdx)
		{
			mPlacementObj.pendingAdbrkId = "";
			mPlacementObj.openPeriodId = "";
			mPlacementObj.curEndNumber = 0;
			mPlacementObj.adNextOffset = 0;
		}
	}
}

int PrivateCDAIObjectMPD::CheckForAdStart(bool continuePlay, const std::string &periodId, double offSet, std::string &breakId, double &adOffset)
{
	int adIdx = -1;
	auto pit = mPeriodMap.find(periodId);
	if(mPeriodMap.end() != pit && !(pit->second.adBreakId.empty()))
	{
		//mBasePeriodId belongs to an Adbreak. Now we need to see whether any Ad is placed in the offset.
		Period2AdData &curP2Ad = pit->second;
		if(isAdBreakObjectExist(curP2Ad.adBreakId))
		{
			breakId = curP2Ad.adBreakId;
			AdBreakObject &abObj = mAdBreaks[breakId];
			if(continuePlay)
			{
				int floorKey = (int)(offSet * 1000);
				floorKey = floorKey - (floorKey%OFFSET_ALIGN_FACTOR);
				auto adIt = curP2Ad.offset2Ad.find(floorKey);
				if(curP2Ad.offset2Ad.end() == adIt)
				{
					//Considering only Ad start
					int ceilKey = floorKey + OFFSET_ALIGN_FACTOR;
					adIt = curP2Ad.offset2Ad.find(ceilKey);
				}

				if((curP2Ad.offset2Ad.end() != adIt) && (0 == adIt->second.adStartOffset))
				{
					//Considering only Ad start
					adIdx = adIt->second.adIdx;
					adOffset = 0;
				}
			}
			else	//Discrete playback
			{
				uint64_t key = (uint64_t)(offSet * 1000);
				uint64_t start = 0;
				uint64_t end = curP2Ad.duration;
				if(periodId ==  abObj.endPeriodId)
				{
					end = abObj.endPeriodOffset;	//No need to look beyond the adbreakEnd
				}

				if(key >= start && key <= end)
				{
					//Yes, Key is in Adbreak. Find which Ad.
					for(auto it = curP2Ad.offset2Ad.begin(); it != curP2Ad.offset2Ad.end(); it++)
					{
						if(key >= it->first)
						{
							adIdx = it->second.adIdx;
							adOffset = (double)((key - it->first)/1000);
						}
						else
						{
							break;
						}
					}
				}
			}

			if(-1 == adIdx && abObj.endPeriodId == periodId && (uint64_t)(offSet*1000) >= abObj.endPeriodOffset)
			{
				breakId = "";	//AdState should not stick to IN_ADBREAK after Adbreak ends.
			}
		}
	}
	return adIdx;
}

bool PrivateCDAIObjectMPD::isPeriodInAdbreak(const std::string &periodId)
{
	return !(mPeriodMap[periodId].adBreakId.empty());
}



/**
 * @brief Get libdash xml Node for Ad period
 * @param[in] manifestUrl url of the Ad
 * @retval libdash xml Node corresponding to Ad period
 */
MPD* PrivateCDAIObjectMPD::GetAdMPD(std::string &manifestUrl, bool &finalManifest, bool tryFog)
{
	MPD* adMpd = NULL;
	GrowableBuffer manifest;
	bool gotManifest = false;
	long http_error = 0;
	std::string effectiveUrl;
	memset(&manifest, 0, sizeof(manifest));
		gotManifest = mAamp->GetFile(manifestUrl, &manifest, effectiveUrl, &http_error, NULL, AAMP_DAI_CURL_IDX);
	if (gotManifest)
	{
		AAMPLOG_TRACE("PrivateCDAIObjectMPD::%s - manifest download success\n", __FUNCTION__);
	}
	else if (mAamp->DownloadsAreEnabled())
	{
		AAMPLOG_ERR("PrivateCDAIObjectMPD::%s - manifest download failed\n", __FUNCTION__);
	}

	if (gotManifest)
	{
		finalManifest = true;
		xmlTextReaderPtr reader = xmlReaderForMemory(manifest.ptr, (int) manifest.len, NULL, NULL, 0);
		if(tryFog && !(gpGlobalConfig->playAdFromCDN) && reader && mIsFogTSB)	//Main content from FOG. Ad is expected from FOG.
		{
			std::string channelUrl = mAamp->GetManifestUrl();	//TODO: Get FOG URL from channel URL
			std::string encodedUrl;
			UrlEncode(effectiveUrl, encodedUrl);
			int ipend = 0;
			for(int slashcnt=0; slashcnt < 3 && channelUrl[ipend]; ipend++)
			{
				if(channelUrl[ipend] == '/') slashcnt++;
			}

			effectiveUrl.assign(channelUrl);
			effectiveUrl.append("adrec?clientId=FOG_AAMP&recordedUrl=");
			effectiveUrl.append(encodedUrl.c_str());
			GrowableBuffer fogManifest;
			memset(&fogManifest, 0, sizeof(manifest));
			http_error = 0;
			mAamp->GetFile(effectiveUrl, &fogManifest, effectiveUrl, &http_error, NULL, AAMP_DAI_CURL_IDX);
			if(200 == http_error || 204 == http_error)
			{
				manifestUrl = effectiveUrl;
				if(200 == http_error)
				{
					//FOG already has the manifest. Releasing the one from CDN and using FOG's
					xmlFreeTextReader(reader);
					reader = xmlReaderForMemory(fogManifest.ptr, (int) fogManifest.len, NULL, NULL, 0);
					aamp_Free(&manifest.ptr);
					manifest = fogManifest;
					fogManifest.ptr = NULL;
				}
				else
				{
					finalManifest = false;
				}
			}

			if(fogManifest.ptr)
			{
				aamp_Free(&fogManifest.ptr);
			}
		}
		if (reader != NULL)
		{
			if (xmlTextReaderRead(reader))
			{
				Node* root = aamp_ProcessNode(&reader, manifestUrl, true);
				if (NULL != root)
				{
					std::vector<Node*> children = root->GetSubNodes();
					for (size_t i = 0; i < children.size(); i++)
					{
						Node* child = children.at(i);
						const std::string& name = child->GetName();
						logprintf("PrivateCDAIObjectMPD::%s - child->name %s\n", __FUNCTION__, name.c_str());
						if (name == "Period")
						{
							logprintf("PrivateCDAIObjectMPD::%s - found period\n", __FUNCTION__);
							std::vector<Node *> children = child->GetSubNodes();
							bool hasBaseUrl = false;
							for (size_t i = 0; i < children.size(); i++)
							{
								if (children.at(i)->GetName() == "BaseURL")
								{
									hasBaseUrl = true;
								}
							}
							if (!hasBaseUrl)
							{
								// BaseUrl not found in the period. Get it from the root and put it in the period
								children = root->GetSubNodes();
								for (size_t i = 0; i < children.size(); i++)
								{
									if (children.at(i)->GetName() == "BaseURL")
									{
										Node* baseUrl = new Node(*children.at(i));
										child->AddSubNode(baseUrl);
										hasBaseUrl = true;
										break;
									}
								}
							}
							if (!hasBaseUrl)
							{
								std::string baseUrlStr = Path::GetDirectoryPath(manifestUrl);
								Node* baseUrl = new Node();
								baseUrl->SetName("BaseURL");
								baseUrl->SetType(Text);
								baseUrl->SetText(baseUrlStr);
								logprintf("PrivateCDAIObjectMPD::%s - manual adding BaseURL Node [%p] text %s\n",
								        __FUNCTION__, baseUrl, baseUrl->GetText().c_str());
								child->AddSubNode(baseUrl);
							}
							break;
						}
					}
					adMpd = root->ToMPD();
					delete root;
				}
				else
				{
					logprintf("%s:%d - Could not create root node\n", __FUNCTION__, __LINE__);
				}
			}
			else
			{
				logprintf("%s:%d - xmlTextReaderRead failed\n", __FUNCTION__, __LINE__);
			}
			xmlFreeTextReader(reader);
		}
		else
		{
			logprintf("%s:%d - xmlReaderForMemory failed\n", __FUNCTION__, __LINE__);
		}

		if (gpGlobalConfig->logging.trace)
		{
			aamp_AppendNulTerminator(&manifest); // make safe for cstring operations
			logprintf("%s:%d - Ad manifest: %s\n", __FUNCTION__, __LINE__, manifest.ptr);
		}
		aamp_Free(&manifest.ptr);
	}
	else
	{
		logprintf("%s:%d - aamp: error on manifest fetch\n", __FUNCTION__, __LINE__);
	}
	return adMpd;
}

void PrivateCDAIObjectMPD::FulFillAdObject()
{
	bool adStatus = false;
	uint64_t startMS = 0;
	uint64_t durationMs = 0;
	bool finalManifest = false;
	MPD *ad = GetAdMPD(mAdFulfillObj.url, finalManifest, true);
	if(ad)
	{
		std::lock_guard<std::mutex> lock(mDaiMtx);
		auto periodId = mAdFulfillObj.periodId;
		if(ad->GetPeriods().size() && isAdBreakObjectExist(periodId))	// Ad has periods && ensuring that the adbreak still exists
		{
			auto &adbreakObj = mAdBreaks[periodId];
			std::shared_ptr<std::vector<AdNode>> adBreakAssets = adbreakObj.ads;
			durationMs = aamp_GetDurationFromRepresentation(ad);

			startMS = adbreakObj.duration;
			adbreakObj.duration += durationMs;

			std::string bPeriodId = "";		//BasePeriodId will be filled on placement
			int bOffset = -1;				//BaseOffset will be filled on placement
			if(0 == adBreakAssets->size())
			{
				//First Ad placement is doing now.
				if(isPeriodExist(periodId))
				{
					mPeriodMap[periodId].offset2Ad[0] = AdOnPeriod{0,0};
				}

				mPlacementObj.pendingAdbrkId = periodId;
				mPlacementObj.openPeriodId = periodId;	//May not be available Now.
				mPlacementObj.curEndNumber = 0;
				mPlacementObj.curAdIdx = 0;
				mPlacementObj.adNextOffset = 0;
				bPeriodId = periodId;
				bOffset = 0;
			}
			if(!finalManifest)
			{
				logprintf("%s:%d: Final manifest to be downloaded from the FOG later. Deleting the manifest got from CDN.\n", __FUNCTION__, __LINE__);
				delete ad;
				ad = NULL;
			}
			adBreakAssets->emplace_back(AdNode{false, false, mAdFulfillObj.adId, mAdFulfillObj.url, durationMs, bPeriodId, bOffset, ad});
			logprintf("%s:%d: New Ad[Id=%s, url=%s] successfully added.\n", __FUNCTION__, __LINE__, mAdFulfillObj.adId.c_str(),mAdFulfillObj.url.c_str());

			adStatus = true;
		}
		else
		{
			logprintf("%s:%d: AdBreadkId[%s] not existing. Dropping the Ad.\n", __FUNCTION__, __LINE__, periodId.c_str());
			delete ad;
		}
	}
	else
	{
		logprintf("%s:%d: Failed to get Ad MPD[%s].\n", __FUNCTION__, __LINE__, mAdFulfillObj.url.c_str());
	}
	mAamp->SendAdResolvedEvent(mAdFulfillObj.adId, adStatus, startMS, durationMs);
}

void PrivateCDAIObjectMPD::SetAlternateContents(const std::string &periodId, const std::string &adId, const std::string &url,  uint64_t startMS)
{
	if("" == adId || "" == url)
	{
		std::lock_guard<std::mutex> lock(mDaiMtx);
		//Putting a place holder
		if(!(isAdBreakObjectExist(periodId)))
		{
			auto adBreakAssets = std::make_shared<std::vector<AdNode>>();
			mAdBreaks.emplace(periodId, AdBreakObject{0, adBreakAssets, "", 0});	//Fix the duration after getting the Ad
			Period2AdData &pData = mPeriodMap[periodId];
			pData.adBreakId = periodId;
		}
	}
	else
	{
		if(mAdObjThreadID)
		{
			//Clearing the previous thread
			int rc = pthread_join(mAdObjThreadID, NULL);
			mAdObjThreadID = 0;
		}
		mAdFulfillObj.periodId = periodId;
		mAdFulfillObj.adId = adId;
		mAdFulfillObj.url = url;
		int ret = pthread_create(&mAdObjThreadID, NULL, &AdFulfillThreadEntry, this);
		if(ret != 0)
		{
			logprintf("%s:%d pthread_create(FulFillAdObject) failed, errno = %d, %s. Rejecting promise.\n", __FUNCTION__, __LINE__, errno, strerror(errno));
			mAamp->SendAdResolvedEvent(mAdFulfillObj.adId, false, 0, 0);
		}
	}
}