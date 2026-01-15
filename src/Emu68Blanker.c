/******************************************************************************
 * 
 * Program:    Emu68Blanker.c
 * Purpose:    Blanker for PiStorm/Emu68.
 * Authors:    Philippe Carpentier, Michal Schulz
 * Target:     PiStorm, Emu68, AmigaOS 3.x
 * Compiler:   SAS/C Amiga Compiler 6.59
 * Repository: https://github.com/flype44/Emu68Blanker
 * Dependancy: https://github.com/michalsc/Emu68-Tools/releases => Developer
 * 
 ******************************************************************************/

#define  _STRICT_ANSI

#include <stdio.h>
#include <dos/dos.h>
#include <exec/exec.h>
#include <intuition/intuition.h>
#include <libraries/commodities.h>
#include <proto/alib.h>
#include <proto/commodities.h>
#include <proto/devicetree.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/mailbox.h>
#include <proto/utility.h>

#include "Emu68Blanker.h"

/******************************************************************************
 * 
 * Defines
 * 
 ******************************************************************************/

#define BLANKER_DELAY 1800
#define BLANKER_PRIORITY 0
#define BLANKER_HOTKEY "control alt b"
#define BLANKER_DESCR "[%s] [%lu secs]"

#define MAILBOXNAME "mailbox.resource"
#define DEVICETREENAME "devicetree.resource"

/******************************************************************************
 * 
 * Externs
 * 
 ******************************************************************************/

extern struct Library * CxBase;
extern struct ExecBase * SysBase;
extern struct DosLibrary * DosBase;
extern struct Library * UtilityBase;
extern struct IntuitionBase * IntuitionBase;

/******************************************************************************
 * 
 * Prototypes
 * 
 ******************************************************************************/

static BOOL BrokerOpen(VOID);
static VOID BrokerClose(VOID);
static VOID BrokerListen(VOID);
static VOID BrokerAppear(VOID);
static VOID SetBlankScreen(ULONG state);

/******************************************************************************
 * 
 * Globals
 * 
 ******************************************************************************/

APTR MailboxBase = NULL;
APTR DeviceTreeBase = NULL;

BOOL bBusy = FALSE;
ULONG signal = -1L;
ULONG cxPortMask = 0;
ULONG cxSignalMask = 0;

CxObj * cxBroker = NULL;
CxObj * cxFilter = NULL;
CxObj * cxCustom = NULL;
CxObj * cxSignal = NULL;
STRPTR * toolTypes = NULL;
struct MsgPort * cxPort = NULL;

TEXT cxHotKeyFull[128];
TEXT cxHotKeyShort[128];
TEXT cxDescription[128];
ULONG cxDelay = BLANKER_DELAY;
ULONG cxPriority = BLANKER_PRIORITY;

STRPTR versionTag = VERSIONTAG;

/******************************************************************************
 * 
 * chkabort()
 * 
 ******************************************************************************/

ULONG chkabort(VOID)
{
	return (0);
}

/******************************************************************************
 * 
 * StrToLower()
 * 
 ******************************************************************************/

STRPTR StrToLower(STRPTR string)
{
	STRPTR s = string;
	
	while (*s) {
		if (*s >= 'A' && *s <= 'Z')
			*s = *s - 'A' + 'a';
		s++;
	}
	
	return string;
}

/******************************************************************************
 * 
 * SetBlankScreen()
 * 
 ******************************************************************************/

static VOID SetBlankScreen(ULONG state)
{
	// RPi mailbox tag identifier
	// TAG_BLANK_SCREEN (0x40002)
	// Compatible with VC4 and VC6.
	
	if (MailboxBase != NULL)
	{
		ULONG command[7];
		
		command[0] = 7 * 4;      // Request size
		command[1] = 0x00000000; // Request code
		command[2] = 0x00040002; // Request tag id
		command[3] = 0x00000004; // Request tag size
		command[4] = 0x00000000; // Request tag code
		command[5] = state;      // Request tag state
		command[6] = 0x00000000; // Request end
		
		MB_RawCommand(command);  // Request call
	}
}

/******************************************************************************
 * 
 * BrokerCustom()
 * 
 ******************************************************************************/

VOID __interrupt __saveds BrokerCustom(register CxMsg * cxMsg, CxObj * cxObj)
{
	struct InputEvent * ie;
	static ULONG time = 0L;
	
	// Check for user inactivity
	if (ie = (struct InputEvent *)CxMsgData(cxMsg)) {
		// Broker timer occurs every ~0.1 second (10Hz)
		if (ie->ie_Class == IECLASS_TIMER) {
			if (!bBusy) {
				time++;
				if (time > cxDelay) {
					time = 0L;
					DivertCxMsg(cxMsg, cxObj, cxObj);
				}
			}
		} else {
			time = 0L;
		}
	}
}

/*****************************************************************************
 * 
 * BrokerOpen()
 * 
 *****************************************************************************/

static BOOL BrokerOpen(VOID)
{
	BOOL result = FALSE;
	
	struct NewBroker newBroker = {
		NB_VERSION,            // nb_Version
		NAME,                  // nb_Name
		TITLE,                 // nb_Title
		DESCRIPTION,           // nb_Descr
		NBU_UNIQUE|NBU_NOTIFY, // nb_Unique
		COF_SHOW_HIDE,         // nb_Flags
		0,                     // nb_Pri
		NULL,                  // nb_Port
		0                      // nb_ReservedChannel
	};
	
	if (cxPort = CreateMsgPort())
	{
		LONG error = 0;
		
		newBroker.nb_Port = cxPort;
		newBroker.nb_Pri = (BYTE)cxPriority;
		newBroker.nb_Descr = cxDescription;
		cxPortMask = 1L << cxPort->mp_SigBit;
		cxBroker = CxBroker(&newBroker, &error);
		
		if (cxBroker && error == CBERR_OK)
		{
			// Create HotKey
			if (cxFilter = HotKey(cxHotKeyFull, cxPort, CXCMD_APPEAR)) {
				AttachCxObj(cxBroker, cxFilter);
			}
			
			// Create CxCustom
			if (cxCustom = CxCustom(BrokerCustom, 0L)) {
				AttachCxObj(cxBroker, cxCustom);
				if ((signal = (ULONG)AllocSignal(-1L)) != -1L) {
					struct Task * task = FindTask(NULL);
					cxSignalMask = 1L << signal;
					cxPortMask |= cxSignalMask;
					if (cxSignal = CxSignal(task, signal))
						AttachCxObj(cxCustom, cxSignal);
				}
			}
			
			// Enable broker
			ActivateCxObj(cxBroker, TRUE);
			result = TRUE;
		}
	}
	
	return (result);
}

/*****************************************************************************
 * 
 * BrokerClose()
 * 
 *****************************************************************************/

static VOID BrokerClose(VOID)
{
	if (signal != -1L) {
		FreeSignal(signal);
	}
	
	if (cxBroker != NULL) {
		DeleteCxObjAll(cxBroker);
		cxBroker = NULL;
	}
	
	if (cxPort != NULL) {
		struct Message * msg;
		while (msg = GetMsg(cxPort))
			ReplyMsg(msg);
		DeleteMsgPort(cxPort);
		cxPort = NULL;
	}
}

/*****************************************************************************
 * 
 * BrokerListen()
 * 
 *****************************************************************************/

static VOID BrokerListen(VOID)
{
	CxMsg * msg;
	ULONG done = FALSE;
	
	while (!done)
	{
		// Wait for new messages
		ULONG signals = Wait(cxPortMask);
		
		// Check incoming messages
		while (msg = (CxMsg *)GetMsg(cxPort))
		{
			ULONG msgID = CxMsgID(msg);
			ULONG msgType = CxMsgType(msg);
			ReplyMsg((struct Message *)msg);
			
			switch (msgType)
			{
			case CXM_IEVENT:
				if (msgID == CXCMD_APPEAR)
					BrokerAppear(); // HotKey
				break;
			case CXM_COMMAND:
				switch (msgID) {
				case CXCMD_APPEAR:
					BrokerAppear(); // Exchange
					break;
				case CXCMD_ENABLE:
					ActivateCxObj(cxBroker, TRUE);
					break;
				case CXCMD_DISABLE:
					ActivateCxObj(cxBroker, FALSE);
					break;
				case CXCMD_KILL:
				case CXCMD_UNIQUE:
					done = TRUE;
					break;
				}
				break;
			}
		}
		
		// Check cxSignal
		if (signals & cxSignalMask)
			BrokerAppear(); // User inactivity
	}
}

/******************************************************************************
 * 
 * BrokerAppear()
 * 
 ******************************************************************************/

static VOID BrokerAppear(VOID)
{
	struct Window * customWindow;
	struct Screen * customScreen;
	struct Screen * publicScreen;
	struct IntuiMessage * msg;
	ULONG startSeconds = 0;
	ULONG done = FALSE;
	bBusy = TRUE;
	
	// Lock public screen
	if (publicScreen = LockPubScreen(NULL))
	{
		// Open custom screen
		if (customScreen = OpenScreenTags(NULL, 
				SA_LikeWorkbench, TRUE,
				SA_ShowTitle, FALSE,
				SA_Type, CUSTOMSCREEN,
				TAG_DONE))
		{
			// Open custom window
			if (customWindow = OpenWindowTags(NULL,
				WA_CustomScreen,  customScreen,
				WA_Left,          0, 
				WA_Top,           0,
				WA_Width,         customScreen->Width,
				WA_Height,        customScreen->Height,
				WA_IDCMP,         IDCMP_INTUITICKS|IDCMP_MOUSEBUTTONS|
				                  IDCMP_MOUSEMOVE|IDCMP_RAWKEY,
				WA_Activate,      TRUE,
				WA_Backdrop,      TRUE,
				WA_Borderless,    TRUE,
				WA_GimmeZeroZero, TRUE,
				WA_ReportMouse,   TRUE,
				WA_RMBTrap,       TRUE,
				TAG_DONE))
			{
				// Show Emu68 screen
				SetBlankScreen(0);
				
				// Handle window events
				while (!done)
				{
					// Wait for new window events
					WaitPort(customWindow->UserPort);
					
					// Process incoming window events
					while (msg = (struct IntuiMessage *)GetMsg(customWindow->UserPort))
					{
						if (startSeconds == 0)
							startSeconds = msg->Seconds;
						
						switch (msg->Class) {
						case IDCMP_MOUSEMOVE:
							done = TRUE;
							break;
						case IDCMP_MOUSEBUTTONS:
						case IDCMP_RAWKEY:
							if ((msg->Seconds - startSeconds) > 0)
								done = TRUE;
							break;
						}
						
						ReplyMsg((struct Message *)msg);
					}
				}
				
				// Hide Emu68 screen
				SetBlankScreen(1);
				
				// Close custom window
				CloseWindow(customWindow);
			}
			
			// Close custom screen
			CloseScreen(customScreen);
		}
		
		// Force intuition to rethink display
		RethinkDisplay();
		WBenchToBack();
		WBenchToFront();
		
		// Unlock public screen
		UnlockPubScreen(NULL, publicScreen);
	}
	
	bBusy = FALSE;
}

/******************************************************************************
 * 
 * main()
 * 
 ******************************************************************************/

ULONG main(ULONG argc, STRPTR * argv)
{
	STRPTR cxHotKey = BLANKER_HOTKEY;
	
	// Open devicetree.resource
	if (!(DeviceTreeBase = OpenResource(DEVICETREENAME))) {
		PutStr("Cant open " DEVICETREENAME " !\n");
		return (RETURN_FAIL);
	}
	
	// Open mailbox.resource
	if (!(MailboxBase = OpenResource(MAILBOXNAME))) {
		PutStr("Cant open " MAILBOXNAME " !\n");
		return (RETURN_FAIL);
	}
	
	// Open tooltypes
	if (toolTypes = ArgArrayInit(argc, argv)) {
		cxDelay = ArgInt(toolTypes, "DELAY", BLANKER_DELAY) * 10;
		cxPriority = ArgInt(toolTypes, "CX_PRIORITY", BLANKER_PRIORITY);
		cxHotKey = StrToLower(ArgString(toolTypes, "HOTKEY", BLANKER_HOTKEY));
	}
	
	// Prepare strings
	sprintf(cxHotKeyShort, "%s", cxHotKey);
	sprintf(cxHotKeyFull, "rawkey %s", cxHotKey);
	sprintf(cxDescription, BLANKER_DESCR, cxHotKeyShort, cxDelay / 10);
	
	// Close tooltypes
	if (toolTypes != NULL)
		ArgArrayDone();
	
	// Open commodity
	if (BrokerOpen()) {
		BrokerListen();
		BrokerClose();
	}
	
	// Return Code
	return (RETURN_OK);
}

/******************************************************************************
 * 
 * End of file
 * 
 ******************************************************************************/
