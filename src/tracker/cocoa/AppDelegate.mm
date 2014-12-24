/*
 *  tracker/cocoa/AppDelegate.mm
 *
 *  Copyright 2014 Dale Whinham
 *
 *  This file is part of Milkytracker.
 *
 *  Milkytracker is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  Milkytracker is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Milkytracker.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#import "AppDelegate.h"

@implementation AppDelegate

// ---------- Display ---------
@synthesize myWindow;
@synthesize myTrackerView;

// ------ Tracker Globals -----
static PPScreen*			myTrackerScreen;
static Tracker*				myTracker;
static PPDisplayDevice*	    myDisplayDevice;
static NSTimer*				myTimer;

static PPMutex*		globalMutex;

static BOOL			startupAfterFullScreen;

// TODO: Crash handler

void RaiseEventSynchronized(PPEvent* event)
{
	if (myTrackerScreen && globalMutex->tryLock())
	{
		myTrackerScreen->raiseEvent(event);
		globalMutex->unlock();
	}
}

- (void)initTracker
{
	[myWindow setTitle:@"Loading MilkyTracker..."];
	[myWindow display];
	
	// Instantiate the tracker
	myTracker = new Tracker();
	
	// Retrieve and apply display settings
	PPSize windowSize = myTracker->getWindowSizeFromDatabase();
	pp_int32 scaleFactor = myTracker->getScreenScaleFactorFromDatabase();
	bool fullScreen = myTracker->getFullScreenFlagFromDatabase();
	
	// Bring up display device
	myDisplayDevice = new PPDisplayDevice(myWindow,
										  myTrackerView,
										  windowSize.width,
										  windowSize.height,
										  scaleFactor,
										  32);
	
	// Enable fullscreen mode if necessary
	myDisplayDevice->goFullScreen(fullScreen);
	
	// Should we wait for fullscreen transition before completing startup?
	startupAfterFullScreen = fullScreen;
	
	// Attach display to tracker
	myTrackerScreen = new PPScreen(myDisplayDevice, myTracker);
	myTracker->setScreen(myTrackerScreen);
}

- (void)trackerStartUp
{
	// Force immediate screen updates during splash screen because Cocoa loop is blocked
	myDisplayDevice->setImmediateUpdates(YES);
	
	// Perform startup
	myTracker->startUp(false);
	
	// Allow Cocoa to handle refresh again (keeps event processing smooth and responsive)
	myDisplayDevice->setImmediateUpdates(NO);
	
	// Timer frequency of 60Hz
	myTimer = [NSTimer scheduledTimerWithTimeInterval:1.0/60.0 target:self selector:@selector(timerCallback:) userInfo:nil repeats:YES];
}

- (void)timerCallback:(NSTimer*)theTimer
{
	if (!myTrackerScreen)
		return;
	
	PPEvent e = PPEvent(eTimer);
	RaiseEventSynchronized(&e);
}

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification
{
	// Initialisation
	globalMutex = new PPMutex();
	[self initTracker];
	
	if (!startupAfterFullScreen)
		[self trackerStartUp];
}

#pragma mark Application events
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender
{
	return myTracker->shutDown() ? NSTerminateNow : NSTerminateCancel;
}

- (void)applicationWillTerminate:(NSNotification *)aNotification
{
	[myTimer invalidate];
	delete myTracker;
	delete myTrackerScreen;
	delete myDisplayDevice;
	delete globalMutex;
}

#pragma mark File open events
- (BOOL)application:(NSApplication *)theApplication openFile:(NSString *)filename
{
	// Temp buffer for file path
	char filePath[PATH_MAX + 1];
	
	// Convert to C string
	[filename getCString:filePath maxLength:PATH_MAX encoding:NSUTF8StringEncoding];
	
	// Create system string from C string
	PPSystemString sysString(filePath);
	PPSystemString* sysStrPtr = &sysString;
	
	// Raise file drop event
	PPEvent event(eFileDragDropped, &sysStrPtr, sizeof(PPSystemString*));
	RaiseEventSynchronized(&event);
	return YES;
}

#pragma mark Window events
- (BOOL)windowShouldClose:(id)sender
{
	[NSApp terminate:self];
	return NO;
}

- (void)windowDidResignKey:(NSNotification *)notification
{
	// Clear modifier keys if window loses focus
	clearKeyModifier(KeyModifierCTRL);
	clearKeyModifier(KeyModifierALT);
	clearKeyModifier(KeyModifierSHIFT);
}

- (void)windowWillEnterFullScreen:(NSNotification *)notification
{
	PPEvent event(eFullScreen);
	RaiseEventSynchronized(&event);
}

- (void)windowWillExitFullScreen:(NSNotification *)notification
{
	PPEvent event(eFullScreen);
	RaiseEventSynchronized(&event);
}

- (void)windowDidEnterFullScreen:(NSNotification *)notification
{
	if (startupAfterFullScreen)
	{
		[self trackerStartUp];
		startupAfterFullScreen = NO;
	}
}
@end
