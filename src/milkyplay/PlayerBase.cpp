/*
 *  PlayerBase.cpp
 *  MilkyPlay core
 *
 *  Created by Peter Barth on Thu Jan 20 2005.
 *  Copyright (c) 2005 milkytracker.net, All rights reserved.
 *
 */

#include "PlayerBase.h"
#include "XModule.h"

mp_sint32 PlayerBase::kick()
{
	// if the player hasn't been initialized until now => DO IT
	if (!isInitialized())
	{
		mp_sint32 err = initDevice();		
		if (err!=0)
		{
			return err;
		}
	}
	
	// - configure player: --------------
	// playing => song is not paused yet
	paused = false;
	// playing => song has not stopped yet
	stopped = false;
	// repeat mode
	this->repeat = repeat;
	// set idle mode
	setIdle(idle);
	
	// - configure mixer: ---------------
	// mixer reset
	resetChannelsWithoutMuting();
	
	// start playing (mixer flag)
	startPlay = true;
	
	// mix buffers
	startMixer();
	
	//SetThreadPriority(hThread,THREAD_PRIORITY_NORMAL);
	// reset sample counters
	sampleCounter = 0;
	return 0;
}

PlayerBase::PlayerBase(mp_uint32 frequency) : 
	ChannelMixer(32, frequency)
{
	module = NULL;
	
	initialNumChannels = 8;
	
	mainVolume	= 255;
	
	rowcnt		= 0;				// counts through each row in a pattern
	poscnt		= 0;				// counts through each index the pattern index table
	synccnt		= 0;
	lastUnvisitedPos = 0;
	
	startPlay						= false;
	paused							= false;
	stopped							= false;
	idle							= false;
	resetOnStopFlag					= false;
	resetMainVolumeOnStartPlayFlag	= true;
	
	adder = BPMCounter = 0;

	patternIndexToPlay = -1;
	
	playMode = PlayMode_Auto;	
}

PlayerBase::~PlayerBase()
{ 
	//if (isPlaying())
	//	stopPlaying();
	
	ChannelMixer::closeDevice(); 
}

void PlayerBase::restart(mp_uint32 startPosition/* = 0*/, mp_uint32 startRow/* = 0*/, bool resetMixer/* = true*/, const mp_ubyte* customPanningTable/* = NULL*/, bool playOneRowOnly /* = false*/)
{
	if (module == NULL) 
		return;

	if (resetMixer)
		resetChannelsWithoutMuting();

	// initialise crappy global variables
	baseBpm = 125;
	
	stopped = false;
	
	synccnt = 0;
	rowcnt = startRow;
	poscnt = startPosition;
	lastUnvisitedPos = poscnt;
	
	synccnt			= 0;

	this->playOneRowOnly = playOneRowOnly;

	if (resetMainVolumeOnStartPlayFlag)
		mainVolume = module->header.mainvol;

	// Clear position/speed lookup tables
	for (mp_uint32 i = 0; i < TIMESAMPLEBUFFERSIZE; i++)
	{
		timeRecordTable[i] = TimeRecord(poscnt, 
										rowcnt, 
										bpm, 
										tickSpeed, 
										mainVolume, 
										ticker);
	}
}

//////////////////////////////////////////////////////
// setup mixer and start playing
// return:   0 = no error
//			-1 = no free device
//			-2 = can't get device ID
//			-3 = can't get device capabilities
//			-4 = device can't handle requested format
//			-5 = can't close device
//			-6 = can't open device
//			-7 = out of memory
//////////////////////////////////////////////////////
mp_sint32 PlayerBase::startPlaying(XModule *module,
							   bool repeat /* = false*/,
							   mp_uint32 startPosition /* = 0*/, 
							   mp_uint32 startRow /* = 0*/,
							   mp_sint32 numChannels /* = -1*/,
							   const mp_ubyte* customPanningTable /* = NULL*/,
							   bool idle /* = false*/,
							   mp_sint32 patternIndex /* = -1*/,
							   bool playOneRowOnly /* = false*/)
{
	this->module = module;
	
	if (numChannels == -1)
		initialNumChannels = module->header.channum;
	else
		initialNumChannels = numChannels;

	ChannelMixer::setNumChannels(initialNumChannels);

	this->idle = idle;
	this->repeat = repeat;
	
	mp_sint32 res = allocateStructures();
	
	if (res != 0)
		return res;

	patternIndexToPlay = patternIndex;

	restart(startPosition, startRow, true, customPanningTable, playOneRowOnly);

	return PlayerBase::kick();	
}

mp_sint32 PlayerBase::stopPlaying()
{
	stop();
	
	mp_sint32 err = closeDevice();
	
	module = NULL;
	
	return err;
}

mp_sint32 PlayerBase::pausePlaying()
{
	if (!paused)
	{
		ChannelMixer::pause();
		
		paused = true;
	}
	return 0;
}

mp_sint32 PlayerBase::resumePlaying(bool unpause/* = true*/)
{
	if (paused && unpause)
	{
		paused = false;
		return resume();
	}
	
	if (module) 
	{
		
		// if the player hasn't been initialized until now => DO IT
		if (!isInitialized())
		{
			mp_sint32 err = initDevice();
			if (err!=0)
			{
				return err;
			}
		}
		
		startMixer();
		
		startPlay = true;
		
	}
	
	return 0;
}

void PlayerBase::nextPattern()
{
	if (!module) return;

	if (startPlay && !paused)
	{
		if (poscnt<module->header.ordnum-1)
		{
			ChannelMixer::resetChannelsWithoutMuting();			
			rowcnt = 0;
			poscnt++;
			lastUnvisitedPos = poscnt;
			clearEffectMemory();
		}
	}

}

void PlayerBase::lastPattern()
{
	if (!module) return;

	if (startPlay && !paused)
	{
		if (poscnt>0)
		{
			//memset(chninfo,0,sizeof(TModuleChannel)*module->header.channum);
			ChannelMixer::resetChannelsWithoutMuting();
			rowcnt = 0;
			poscnt--;
			lastUnvisitedPos = poscnt;
			clearEffectMemory();
		}
	}
}

void PlayerBase::setPatternPos(mp_uint32 pos, mp_uint32 row/* = 0*/, bool resetChannels/* = true*/, bool resetFXMemory/* = true*/)
{
	
	if (!module) return;

	if (startPlay && !paused && (pos < module->header.ordnum))
	{
		//memset(chninfo,0,sizeof(TModuleChannel)*module->header.channum);
		if (resetChannels)
			ChannelMixer::resetChannelsWithoutMuting();

		poscnt = pos;
		rowcnt = row;
		lastUnvisitedPos = poscnt;
		
		updateTimeRecord();
		
		if (resetFXMemory)
			clearEffectMemory();
	}
}


void PlayerBase::timerHandler(mp_sint32 currentBeatPacket)
{
	timeRecordTable[currentBeatPacket] = TimeRecord(poscnt, 
													rowcnt, 
													bpm, 
													tickSpeed, 
													mainVolume,
													ticker);
}