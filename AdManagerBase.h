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
 * @file AdManagerBase.h
 * @brief Client side DAI base class, and common data structures
 */


#ifndef ADMANAGERBASE_H_
#define ADMANAGERBASE_H_

#include "priv_aamp.h"

/**
 * @brief Ad playback states
 */
enum class AdState
{
	OUTSIDE_ADBREAK,		//Not in adbreak, wait for period change
	IN_ADBREAK_AD_NOT_PLAYING,	//Base period in adbreak: But Ad not found/playing
	IN_ADBREAK_AD_PLAYING,		//Ad playing
	IN_ADBREAK_AD_READY2PLAY,	//Ready to play next Ad
	IN_ADBREAK_WAIT2CATCHUP		//Waiting for base period to catchup
};

static constexpr const char *ADSTATE_STR[] =
{
	(const char *)"OUTSIDE_ADBREAK",
	(const char *)"IN_ADBREAK_AD_NOT_PLAYING",
	(const char *)"IN_ADBREAK_AD_PLAYING",
	(const char *)"IN_ADBREAK_AD_READY2PLAY",
	(const char *)"IN_ADBREAK_WAIT2CATCHUP"
};

/**
 * @brief Base class for the client side DAI object
 */
class CDAIObject
{
public:
	/**
	 * @brief CDAIObject constructor.
	 */
	CDAIObject(PrivateInstanceAAMP* aamp)
	{

	}

	/**
	 * @brief CDAIObject destructor.
	 */
	virtual ~CDAIObject()
	{

	}

	/**
	 *   @brief Setting the alternate contents' (Ads/blackouts) URL
	 *
	 *   @param[in] adBreakId - Adbreak's unique identifier.
	 *   @param[in] adId - Individual Ad's id
	 *   @param[in] url - Ad URL
	 *   @param[in] startMS - Ad start time in milliseconds
	 */
	virtual void SetAlternateContents(const std::string &adBreakId, const std::string &adId, const std::string &url, uint64_t startMS=0){}
};



#endif /* ADMANAGERBASE_H_ */