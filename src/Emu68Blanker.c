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
#include "Emu68Blanker.h"

/******************************************************************************
 * 
 * Defines
 * 
 ******************************************************************************/

#define MAILBOXNAME       "mailbox.resource"
#define DEVICETREENAME    "devicetree.resource"
#define BLANKER_HOTKEY    "rawkey control alt b"
#define BLANKER_DELAY     60
#define BLANKER_PRIORITY  0

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
CxObj * cxBroker = NULL;
CxObj * cxFilter = NULL;
CxObj * cxCustom = NULL;
CxObj * cxSignal = NULL;
ULONG signal = -1L;
ULONG cxPortMask = 0;
ULONG cxSignalMask = 0;
ULONG cxDelay = BLANKER_DELAY;
ULONG cxPriority = BLANKER_PRIORITY;
STRPTR cxHotKey = BLANKER_HOTKEY;
struct Task * task = NULL;
struct MsgPort * cxPort = NULL;
STRPTR * toolTypes = NULL;
STRPTR versionTag = VERSIONTAG;
BOOL bBusy = FALSE;

/******************************************************************************
 * 
 * Externs
 * 
 ******************************************************************************/

extern struct Library * CxBase;
extern struct ExecBase * SysBase;
extern struct DosLibrary * DosBase;
extern struct IntuitionBase * IntuitionBase;

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
 * SetBlankScreen()
 * 
 ******************************************************************************/

static VOID SetBlankScreen(ULONG state)
{
	ULONG words[7];
	
	words[0] = 7 * 4;      // Mailbox Request size
	words[1] = 0x00000000; // Mailbox Request code
	words[2] = 0x00040002; // Mailbox Request tag id
	words[3] = 0x00000004; // Mailbox Request tag size
	words[4] = 0x00000000; // Mailbox Request tag code
	words[5] = state;      // Mailbox Request tag state
	words[6] = 0x00000000; // Mailbox Request end
	
	MB_RawCommand(words);
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
	
	if (ie = (struct InputEvent *)CxMsgData(cxMsg))
	{
		if (ie->ie_Class == IECLASS_TIMER)
		{
			if (!bBusy)
			{
				time++;
				
				if (time > cxDelay)
				{
					time = 0L;
					DivertCxMsg(cxMsg, cxObj, cxObj);
				}
			}
		}
		else
		{
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
		cxPortMask = 1L << cxPort->mp_SigBit;
		cxBroker = CxBroker(&newBroker, &error);
		
		if (cxBroker && error == CBERR_OK)
		{
			// Create HotKey
			if (cxFilter = HotKey(cxHotKey, cxPort, CXCMD_APPEAR))
				AttachCxObj(cxBroker, cxFilter);
			
			// Create CxCustom
			if (cxCustom = CxCustom(BrokerCustom, 0L))
			{
				AttachCxObj(cxBroker, cxCustom);
				
				if ((signal = (ULONG)AllocSignal(-1L)) != -1)
				{
					task = FindTask(NULL);
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
				switch (msgID) {
				case CXCMD_APPEAR:
					BrokerAppear();
					break;
				}
				break;
			case CXM_COMMAND:
				switch (msgID) {
				case CXCMD_APPEAR:
					BrokerAppear();
					break;
				case CXCMD_ENABLE:
					ActivateCxObj(cxBroker, TRUE);
					break;
				case CXCMD_DISABLE:
					ActivateCxObj(cxBroker, FALSE);
					break;
				case CXCMD_KILL:
					done = TRUE;
					break;
				case CXCMD_UNIQUE:
					done = TRUE;
					break;
				}
				break;
			}
		}
		
		// Check cxSignal
		if (signals & cxSignalMask)
			BrokerAppear();
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
				
				// Handle events
				while (!done)
				{
					// Wait for new events
					WaitPort(customWindow->UserPort);
					
					// Process incoming events
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
		cxPriority = ArgInt(toolTypes, "CX_PRIORITY", BLANKER_PRIORITY);
		cxHotKey = ArgString(toolTypes, "HOTKEY", BLANKER_HOTKEY);
		cxDelay = ArgInt(toolTypes, "DELAY", BLANKER_DELAY) * 10;
	}
	
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
