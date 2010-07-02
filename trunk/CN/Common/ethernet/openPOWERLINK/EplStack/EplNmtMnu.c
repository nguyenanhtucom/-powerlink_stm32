/****************************************************************************

  (c) SYSTEC electronic GmbH, D-07973 Greiz, August-Bebel-Str. 29
      www.systec-electronic.com

  Project:      openPOWERLINK

  Description:  source file for NMT-MN-Module

  License:

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions
    are met:

    1. Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.

    2. Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.

    3. Neither the name of SYSTEC electronic GmbH nor the names of its
       contributors may be used to endorse or promote products derived
       from this software without prior written permission. For written
       permission, please contact info@systec-electronic.com.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
    "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
    LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
    FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
    COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
    INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
    BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
    LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
    CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
    LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
    ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
    POSSIBILITY OF SUCH DAMAGE.

    Severability Clause:

        If a provision of this License is or becomes illegal, invalid or
        unenforceable in any jurisdiction, that shall not affect:
        1. the validity or enforceability in that jurisdiction of any other
           provision of this License; or
        2. the validity or enforceability in other jurisdictions of that or
           any other provision of this License.

  -------------------------------------------------------------------------

                $RCSfile: EplNmtMnu.c,v $

                $Author: D.Krueger $

                $Revision: 1.30 $  $Date: 2010/01/08 15:41:33 $

                $State: Exp $

                Build Environment:
                    GCC V3.4

  -------------------------------------------------------------------------

  Revision History:

  2006/06/09 k.t.:   start of the implementation

****************************************************************************/

#include "user/EplNmtMnu.h"
#include "user/EplTimeru.h"
#include "user/EplIdentu.h"
#include "user/EplStatusu.h"
#include "user/EplObdu.h"
#include "user/EplDlluCal.h"
#include "Benchmark.h"

#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) != 0)

#if (((EPL_MODULE_INTEGRATION) & (EPL_MODULE_OBDU)) == 0) && (EPL_OBD_USE_KERNEL == FALSE)
#error "EPL NmtMnu module needs EPL module OBDU or OBDK!"
#endif

//=========================================================================//
//                                                                         //
//          P R I V A T E   D E F I N I T I O N S                          //
//                                                                         //
//=========================================================================//

//---------------------------------------------------------------------------
// const defines
//---------------------------------------------------------------------------

// TracePoint support for realtime-debugging
#ifdef _DBG_TRACE_POINTS_
    void  PUBLIC  TgtDbgSignalTracePoint (BYTE bTracePointNumber_p);
    void  PUBLIC  TgtDbgPostTraceValue (DWORD dwTraceValue_p);
    #define TGT_DBG_SIGNAL_TRACE_POINT(p)   TgtDbgSignalTracePoint(p)
    #define TGT_DBG_POST_TRACE_VALUE(v)     TgtDbgPostTraceValue(v)
#else
    #define TGT_DBG_SIGNAL_TRACE_POINT(p)
    #define TGT_DBG_POST_TRACE_VALUE(v)
#endif
#define EPL_NMTMNU_DBG_POST_TRACE_VALUE(Event_p, uiNodeId_p, wErrorCode_p) \
//    TGT_DBG_POST_TRACE_VALUE((kEplEventSinkNmtMnu << 28) | (Event_p << 24) \
                             | (uiNodeId_p << 16) | wErrorCode_p)


// defines for flags in node info structure
#define EPL_NMTMNU_NODE_FLAG_ISOCHRON       0x0001  // CN is being accessed isochronously
#define EPL_NMTMNU_NODE_FLAG_NOT_SCANNED    0x0002  // CN was not scanned once -> decrement SignalCounter and reset flag
#define EPL_NMTMNU_NODE_FLAG_HALTED         0x0004  // boot process for this CN is halted
#define EPL_NMTMNU_NODE_FLAG_NMT_CMD_ISSUED 0x0008  // NMT command was just issued, wrong NMT states will be tolerated
#define EPL_NMTMNU_NODE_FLAG_COUNT_STATREQ  0x0300  // counter for StatusRequest timer handle
#define EPL_NMTMNU_NODE_FLAG_COUNT_LONGER   0x0C00  // counter for longer timeouts timer handle
#define EPL_NMTMNU_NODE_FLAG_INC_STATREQ    0x0100  // increment for StatusRequest timer handle
#define EPL_NMTMNU_NODE_FLAG_INC_LONGER     0x0400  // increment for longer timeouts timer handle
                    // These counters will be incremented at every timer start
                    // and copied to timerarg. When the timer event occures
                    // both will be compared and if unequal the timer event
                    // will be discarded, because it is an old one.

// defines for timer arguments to draw a distinction between serveral events
#define EPL_NMTMNU_TIMERARG_NODE_MASK   0x000000FFL // mask that contains the node-ID
#define EPL_NMTMNU_TIMERARG_IDENTREQ    0x00010000L // timer event is for IdentRequest
#define EPL_NMTMNU_TIMERARG_STATREQ     0x00020000L // timer event is for StatusRequest
#define EPL_NMTMNU_TIMERARG_LONGER      0x00040000L // timer event is for longer timeouts
#define EPL_NMTMNU_TIMERARG_STATE_MON   0x00080000L // timer event for StatusRequest to monitor execution of NMT state changes
#define EPL_NMTMNU_TIMERARG_COUNT_SR    0x00000300L // counter for StatusRequest
#define EPL_NMTMNU_TIMERARG_COUNT_LO    0x00000C00L // counter for longer timeouts
                    // The counters must have the same position as in the node flags above.

#define EPL_NMTMNU_SET_FLAGS_TIMERARG_STATREQ(pNodeInfo_p, uiNodeId_p, TimerArg_p) \
    pNodeInfo_p->m_wFlags = \
        ((pNodeInfo_p->m_wFlags + EPL_NMTMNU_NODE_FLAG_INC_STATREQ) \
         & EPL_NMTMNU_NODE_FLAG_COUNT_STATREQ) \
        | (pNodeInfo_p->m_wFlags & ~EPL_NMTMNU_NODE_FLAG_COUNT_STATREQ); \
    TimerArg_p.m_Arg.m_dwVal = EPL_NMTMNU_TIMERARG_STATREQ | uiNodeId_p | \
        (pNodeInfo_p->m_wFlags & EPL_NMTMNU_NODE_FLAG_COUNT_STATREQ); \
    TimerArg_p.m_EventSink = kEplEventSinkNmtMnu;

#define EPL_NMTMNU_SET_FLAGS_TIMERARG_IDENTREQ(pNodeInfo_p, uiNodeId_p, TimerArg_p) \
    pNodeInfo_p->m_wFlags = \
        ((pNodeInfo_p->m_wFlags + EPL_NMTMNU_NODE_FLAG_INC_STATREQ) \
         & EPL_NMTMNU_NODE_FLAG_COUNT_STATREQ) \
        | (pNodeInfo_p->m_wFlags & ~EPL_NMTMNU_NODE_FLAG_COUNT_STATREQ); \
    TimerArg_p.m_Arg.m_dwVal = EPL_NMTMNU_TIMERARG_IDENTREQ | uiNodeId_p | \
        (pNodeInfo_p->m_wFlags & EPL_NMTMNU_NODE_FLAG_COUNT_STATREQ); \
    TimerArg_p.m_EventSink = kEplEventSinkNmtMnu;

#define EPL_NMTMNU_SET_FLAGS_TIMERARG_LONGER(pNodeInfo_p, uiNodeId_p, TimerArg_p) \
    pNodeInfo_p->m_wFlags = \
        ((pNodeInfo_p->m_wFlags + EPL_NMTMNU_NODE_FLAG_INC_LONGER) \
         & EPL_NMTMNU_NODE_FLAG_COUNT_LONGER) \
        | (pNodeInfo_p->m_wFlags & ~EPL_NMTMNU_NODE_FLAG_COUNT_LONGER); \
    TimerArg_p.m_Arg.m_dwVal = EPL_NMTMNU_TIMERARG_LONGER | uiNodeId_p | \
        (pNodeInfo_p->m_wFlags & EPL_NMTMNU_NODE_FLAG_COUNT_LONGER); \
    TimerArg_p.m_EventSink = kEplEventSinkNmtMnu;

#define EPL_NMTMNU_SET_FLAGS_TIMERARG_STATE_MON(pNodeInfo_p, uiNodeId_p, TimerArg_p) \
    pNodeInfo_p->m_wFlags = \
        ((pNodeInfo_p->m_wFlags + EPL_NMTMNU_NODE_FLAG_INC_STATREQ) \
         & EPL_NMTMNU_NODE_FLAG_COUNT_STATREQ) \
        | (pNodeInfo_p->m_wFlags & ~EPL_NMTMNU_NODE_FLAG_COUNT_STATREQ); \
    TimerArg_p.m_Arg.m_dwVal = EPL_NMTMNU_TIMERARG_STATE_MON | uiNodeId_p | \
        (pNodeInfo_p->m_wFlags & EPL_NMTMNU_NODE_FLAG_COUNT_STATREQ); \
    TimerArg_p.m_EventSink = kEplEventSinkNmtMnu;


// defines for global flags
#define EPL_NMTMNU_FLAG_HALTED          0x0001  // boot process is halted
#define EPL_NMTMNU_FLAG_APP_INFORMED    0x0002  // application was informed about possible NMT state change
#define EPL_NMTMNU_FLAG_USER_RESET      0x0004  // NMT reset issued by user / diagnostic node

// return pointer to node info structure for specified node ID
// d.k. may be replaced by special (hash) function if node ID array is smaller than 254
#define EPL_NMTMNU_GET_NODEINFO(uiNodeId_p) (&EplNmtMnuInstance_g.m_aNodeInfo[uiNodeId_p - 1])

//---------------------------------------------------------------------------
// local types
//---------------------------------------------------------------------------

typedef struct
{
    unsigned int        m_uiNodeId;
    tEplNmtNodeCommand  m_NodeCommand;

} tEplNmtMnuNodeCmd;


typedef enum
{
    kEplNmtMnuIntNodeEventNoIdentResponse   = 0x00,
    kEplNmtMnuIntNodeEventIdentResponse     = 0x01,
    kEplNmtMnuIntNodeEventBoot              = 0x02,
    kEplNmtMnuIntNodeEventExecResetConf     = 0x03,
    kEplNmtMnuIntNodeEventExecResetNode     = 0x04,
    kEplNmtMnuIntNodeEventConfigured        = 0x05,
    kEplNmtMnuIntNodeEventNoStatusResponse  = 0x06,
    kEplNmtMnuIntNodeEventStatusResponse    = 0x07,
    kEplNmtMnuIntNodeEventHeartbeat         = 0x08,
    kEplNmtMnuIntNodeEventNmtCmdSent        = 0x09,
    kEplNmtMnuIntNodeEventTimerIdentReq     = 0x0A,
    kEplNmtMnuIntNodeEventTimerStatReq      = 0x0B,
    kEplNmtMnuIntNodeEventTimerStateMon     = 0x0C,
    kEplNmtMnuIntNodeEventTimerLonger       = 0x0D,
    kEplNmtMnuIntNodeEventError             = 0x0E,

} tEplNmtMnuIntNodeEvent;


typedef enum
{
    kEplNmtMnuNodeStateUnknown      = 0x00,
    kEplNmtMnuNodeStateIdentified   = 0x01,
    kEplNmtMnuNodeStateConfRestored = 0x02, // CN ResetNode after restore configuration
    kEplNmtMnuNodeStateResetConf    = 0x03, // CN ResetConf after configuration update
    kEplNmtMnuNodeStateConfigured   = 0x04, // BootStep1 completed
    kEplNmtMnuNodeStateReadyToOp    = 0x05, // BootStep2 completed
    kEplNmtMnuNodeStateComChecked   = 0x06, // Communication checked successfully
    kEplNmtMnuNodeStateOperational  = 0x07, // CN is in NMT state OPERATIONAL

} tEplNmtMnuNodeState;


typedef struct
{
    tEplTimerHdl        m_TimerHdlStatReq;  // timer to delay StatusRequests and IdentRequests
    tEplTimerHdl        m_TimerHdlLonger;   // 2nd timer for NMT command EnableReadyToOp and CheckCommunication
    tEplNmtMnuNodeState m_NodeState;    // internal node state (kind of sub state of NMT state)
    DWORD               m_dwNodeCfg;    // subindex from 0x1F81
    WORD                m_wFlags;       // flags: CN is being accessed isochronously

} tEplNmtMnuNodeInfo;


typedef struct
{
    tEplNmtMnuNodeInfo  m_aNodeInfo[EPL_NMT_MAX_NODE_ID];
    tEplTimerHdl        m_TimerHdlNmtState;     // timeout for stay in NMT state
    unsigned int        m_uiMandatorySlaveCount;
    unsigned int        m_uiSignalSlaveCount;
    unsigned long       m_ulStatusRequestDelay; // in [ms] (object 0x1006 * EPL_C_NMT_STATREQ_CYCLE)
    unsigned long       m_ulTimeoutReadyToOp;   // in [ms] (object 0x1F89/5)
    unsigned long       m_ulTimeoutCheckCom;    // in [ms] (object 0x1006 * MultiplexedCycleCount)
    WORD                m_wFlags;               // global flags
    DWORD               m_dwNmtStartup;         // object 0x1F80 NMT_StartUp_U32
    tEplNmtMnuCbNodeEvent m_pfnCbNodeEvent;
    tEplNmtMnuCbBootEvent m_pfnCbBootEvent;

} tEplNmtMnuInstance;


//---------------------------------------------------------------------------
// local vars
//---------------------------------------------------------------------------

static tEplNmtMnuInstance   EplNmtMnuInstance_g;


//---------------------------------------------------------------------------
// local function prototypes
//---------------------------------------------------------------------------

static tEplKernel PUBLIC EplNmtMnuCbNmtRequest(tEplFrameInfo * pFrameInfo_p);

static tEplKernel PUBLIC EplNmtMnuCbIdentResponse(
                                    unsigned int        uiNodeId_p,
                                    tEplIdentResponse*  pIdentResponse_p);

static tEplKernel PUBLIC EplNmtMnuCbStatusResponse(
                                    unsigned int        uiNodeId_p,
                                    tEplStatusResponse* pStatusResponse_p);

static tEplKernel EplNmtMnuCheckNmtState(
                                    unsigned int        uiNodeId_p,
                                    tEplNmtMnuNodeInfo* pNodeInfo_p,
                                    tEplNmtState        NodeNmtState_p,
                                    WORD                wErrorCode_p,
                                    tEplNmtState        LocalNmtState_p);

static tEplKernel EplNmtMnuStartBootStep1(BOOL fNmtResetAllIssued_p);

static tEplKernel EplNmtMnuStartBootStep2(void);

static tEplKernel EplNmtMnuStartCheckCom(void);

static tEplKernel EplNmtMnuNodeBootStep2(unsigned int uiNodeId_p, tEplNmtMnuNodeInfo* pNodeInfo_p);

static tEplKernel EplNmtMnuNodeCheckCom(unsigned int uiNodeId_p, tEplNmtMnuNodeInfo* pNodeInfo_p);

static tEplKernel EplNmtMnuStartNodes(void);

static tEplKernel EplNmtMnuProcessInternalEvent(
                                    unsigned int        uiNodeId_p,
                                    tEplNmtState        NodeNmtState_p,
                                    WORD                wErrorCode_p,
                                    tEplNmtMnuIntNodeEvent NodeEvent_p);

static tEplKernel EplNmtMnuReset(void);



//=========================================================================//
//                                                                         //
//          P U B L I C   F U N C T I O N S                                //
//                                                                         //
//=========================================================================//

//---------------------------------------------------------------------------
//
// Function:    EplNmtMnuInit
//
// Description: init first instance of the module
//
//
//
// Parameters:
//
//
// Returns:     tEplKernel  = errorcode
//
//
// State:
//
//---------------------------------------------------------------------------

tEplKernel EplNmtMnuInit(tEplNmtMnuCbNodeEvent pfnCbNodeEvent_p,
                         tEplNmtMnuCbBootEvent pfnCbBootEvent_p)
{
tEplKernel Ret;

    Ret = EplNmtMnuAddInstance(pfnCbNodeEvent_p, pfnCbBootEvent_p);

    return Ret;
}


//---------------------------------------------------------------------------
//
// Function:    EplNmtMnuAddInstance
//
// Description: init other instances of the module
//
//
//
// Parameters:
//
//
// Returns:     tEplKernel  = errorcode
//
//
// State:
//
//---------------------------------------------------------------------------

tEplKernel EplNmtMnuAddInstance(tEplNmtMnuCbNodeEvent pfnCbNodeEvent_p,
                                tEplNmtMnuCbBootEvent pfnCbBootEvent_p)
{
tEplKernel Ret;

    Ret = kEplSuccessful;

    // reset instance structure
    EPL_MEMSET(&EplNmtMnuInstance_g, 0, sizeof (EplNmtMnuInstance_g));

    if ((pfnCbNodeEvent_p == NULL) || (pfnCbBootEvent_p == NULL))
    {
        Ret = kEplNmtInvalidParam;
        goto Exit;
    }
    EplNmtMnuInstance_g.m_pfnCbNodeEvent = pfnCbNodeEvent_p;
    EplNmtMnuInstance_g.m_pfnCbBootEvent = pfnCbBootEvent_p;

    // initialize StatusRequest delay
    EplNmtMnuInstance_g.m_ulStatusRequestDelay = 5000L;

    // register NmtMnResponse callback function
    Ret = EplDlluCalRegAsndService(kEplDllAsndNmtRequest, EplNmtMnuCbNmtRequest, kEplDllAsndFilterLocal);

Exit:
    return Ret;

}


//---------------------------------------------------------------------------
//
// Function:    EplNmtMnuDelInstance
//
// Description: delete instance
//
//
//
// Parameters:
//
//
// Returns:     tEplKernel  = errorcode
//
//
// State:
//
//---------------------------------------------------------------------------

tEplKernel EplNmtMnuDelInstance(void)
{
tEplKernel  Ret;

    Ret = kEplSuccessful;

    // deregister NmtMnResponse callback function
    Ret = EplDlluCalRegAsndService(kEplDllAsndNmtRequest, NULL, kEplDllAsndFilterNone);

    Ret = EplNmtMnuReset();

    return Ret;

}


//---------------------------------------------------------------------------
//
// Function:    EplNmtMnuSendNmtCommandEx
//
// Description: sends the specified NMT command to the specified node.
//
// Parameters:  uiNodeId_p              = node ID to which the NMT command will be sent
//              NmtCommand_p            = NMT command
//
// Returns:     tEplKernel              = error code
//
// State:
//
//---------------------------------------------------------------------------

tEplKernel EplNmtMnuSendNmtCommandEx(unsigned int uiNodeId_p,
                                    tEplNmtCommand  NmtCommand_p,
                                    void* pNmtCommandData_p,
                                    unsigned int uiDataSize_p)
{
tEplKernel      Ret = kEplSuccessful;
tEplFrameInfo   FrameInfo;
BYTE            abBuffer[EPL_C_DLL_MINSIZE_NMTCMDEXT];
tEplFrame*      pFrame = (tEplFrame*) abBuffer;
tEplDllNodeOpParam  NodeOpParam;

    if ((uiNodeId_p == 0) || (uiNodeId_p > EPL_C_ADR_BROADCAST))
    {   // invalid node ID specified
        Ret = kEplInvalidNodeId;
        goto Exit;
    }

    if ((pNmtCommandData_p != NULL) && (uiDataSize_p > (EPL_C_DLL_MINSIZE_NMTCMDEXT - EPL_C_DLL_MINSIZE_NMTCMD)))
    {
        Ret = kEplNmtInvalidParam;
        goto Exit;
    }

    // $$$ d.k. may be check in future versions if the caller wants to perform prohibited state transitions
    //     the CN should not perform these transitions, but the expected NMT state will be changed and never fullfilled.

    // build frame
    EPL_MEMSET(pFrame, 0x00, sizeof(abBuffer));
    AmiSetByteToLe(&pFrame->m_le_bDstNodeId, (BYTE) uiNodeId_p);
    AmiSetByteToLe(&pFrame->m_Data.m_Asnd.m_le_bServiceId, (BYTE) kEplDllAsndNmtCommand);
    AmiSetByteToLe(&pFrame->m_Data.m_Asnd.m_Payload.m_NmtCommandService.m_le_bNmtCommandId,
        (BYTE)NmtCommand_p);
    if ((pNmtCommandData_p != NULL) && (uiDataSize_p > 0))
    {   // copy command data to frame
        EPL_MEMCPY(&pFrame->m_Data.m_Asnd.m_Payload.m_NmtCommandService.m_le_abNmtCommandData[0], pNmtCommandData_p, uiDataSize_p);
    }

    // build info structure
    FrameInfo.m_pFrame = pFrame;
    FrameInfo.m_uiFrameSize = sizeof(abBuffer);

    // send NMT-Request
#if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_DLLU)) != 0)
    Ret = EplDlluCalAsyncSend(&FrameInfo,    // pointer to frameinfo
                           kEplDllAsyncReqPrioNmt); // priority
#endif
    if (Ret != kEplSuccessful)
    {
        goto Exit;
    }

    //EPL_DBGLVL_NMTMN_TRACE2("NMTCmd(%02X->%02X)\n", NmtCommand_p, uiNodeId_p);

    switch (NmtCommand_p)
    {
        case kEplNmtCmdStartNode:
        case kEplNmtCmdEnterPreOperational2:
        case kEplNmtCmdEnableReadyToOperate:
        {
            // nothing left to do,
            // because any further processing is done
            // when the NMT command is actually sent
            goto Exit;
        }

        case kEplNmtCmdStopNode:
        {
            // remove CN from isochronous phase softly
            NodeOpParam.m_OpNodeType = kEplDllNodeOpTypeSoftDelete;
            break;
        }

        case kEplNmtCmdResetNode:
        case kEplNmtCmdResetCommunication:
        case kEplNmtCmdResetConfiguration:
        case kEplNmtCmdSwReset:
        {
            // remove CN immediately from isochronous phase
            NodeOpParam.m_OpNodeType = kEplDllNodeOpTypeIsochronous;
            break;
        }

        default:
            goto Exit;
    }

    // The expected node state will be updated when the NMT command
    // was actually sent.
    // See functions EplNmtMnuProcessInternalEvent(kEplNmtMnuIntNodeEventNmtCmdSent),
    // EplNmtMnuProcessEvent(kEplEventTypeNmtMnuNmtCmdSent).

    // remove CN from isochronous phase;
    // This must be done here and not when NMT command is actually sent
    // because it will be too late and may cause unwanted errors
    if (uiNodeId_p != EPL_C_ADR_BROADCAST)
    {
        NodeOpParam.m_uiNodeId = uiNodeId_p;
        Ret = EplDlluCalDeleteNode(&NodeOpParam);
    }
    else
    {   // do it for all active CNs
        for (uiNodeId_p = 1; uiNodeId_p <= tabentries(EplNmtMnuInstance_g.m_aNodeInfo); uiNodeId_p++)
        {
            if ((EPL_NMTMNU_GET_NODEINFO(uiNodeId_p)->m_dwNodeCfg & (EPL_NODEASSIGN_NODE_IS_CN | EPL_NODEASSIGN_NODE_EXISTS)) != 0)
            {
                NodeOpParam.m_uiNodeId = uiNodeId_p;
                Ret = EplDlluCalDeleteNode(&NodeOpParam);
            }
        }
    }

Exit:
    return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplNmtMnuSendNmtCommand
//
// Description: sends the specified NMT command to the specified node.
//
// Parameters:  uiNodeId_p              = node ID to which the NMT command will be sent
//              NmtCommand_p            = NMT command
//
// Returns:     tEplKernel              = error code
//
// State:
//
//---------------------------------------------------------------------------

tEplKernel EplNmtMnuSendNmtCommand(unsigned int uiNodeId_p,
                                    tEplNmtCommand  NmtCommand_p)
{
tEplKernel      Ret = kEplSuccessful;

    Ret = EplNmtMnuSendNmtCommandEx(uiNodeId_p, NmtCommand_p, NULL, 0);

//Exit:
    return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplNmtMnuRequestNmtCommand
//
// Description: requests the specified NMT command for the specified node.
//              It may also be applied to the local node.
//
// Parameters:  uiNodeId_p              = node ID to which the NMT command will be sent
//              NmtCommand_p            = NMT command
//
// Returns:     tEplKernel              = error code
//
// State:
//
//---------------------------------------------------------------------------

tEplKernel EplNmtMnuRequestNmtCommand(unsigned int uiNodeId_p,
                                    tEplNmtCommand  NmtCommand_p)
{
tEplKernel      Ret = kEplSuccessful;
tEplNmtState    NmtState;

    NmtState = EplNmtuGetNmtState();

    if (NmtState <= kEplNmtMsNotActive)
    {
        Ret = kEplInvalidOperation;
        goto Exit;
    }

    if (uiNodeId_p > EPL_C_ADR_BROADCAST)
    {
        Ret = kEplInvalidNodeId;
        goto Exit;
    }

    if (uiNodeId_p == 0x00)
    {
        uiNodeId_p = EPL_C_ADR_MN_DEF_NODE_ID;
    }

    if (uiNodeId_p == EPL_C_ADR_MN_DEF_NODE_ID)
    {   // apply command to local node-ID

        switch (NmtCommand_p)
        {
            case kEplNmtCmdIdentResponse:
            {   // issue request for local node
                Ret = EplIdentuRequestIdentResponse(0x00, NULL);
                goto Exit;
            }

            case kEplNmtCmdStatusResponse:
            {   // issue request for local node
                Ret = EplStatusuRequestStatusResponse(0x00, NULL);
                goto Exit;
            }

            case kEplNmtCmdResetNode:
            case kEplNmtCmdResetCommunication:
            case kEplNmtCmdResetConfiguration:
            case kEplNmtCmdSwReset:
            {
                uiNodeId_p = EPL_C_ADR_BROADCAST;
                break;
            }

            case kEplNmtCmdInvalidService:
            default:
            {
                Ret = kEplObdAccessViolation;
                goto Exit;
            }
        }
    }

    if (uiNodeId_p != EPL_C_ADR_BROADCAST)
    {   // apply command to remote node-ID, but not broadcast
    tEplNmtMnuNodeInfo* pNodeInfo;

        pNodeInfo = EPL_NMTMNU_GET_NODEINFO(uiNodeId_p);

        switch (NmtCommand_p)
        {
            case kEplNmtCmdIdentResponse:
            {   // issue request for remote node
                // if it is a non-existing node or no identrequest is running
                if (((pNodeInfo->m_dwNodeCfg
                     & (EPL_NODEASSIGN_NODE_IS_CN | EPL_NODEASSIGN_NODE_EXISTS))
                        != (EPL_NODEASSIGN_NODE_IS_CN | EPL_NODEASSIGN_NODE_EXISTS))
                    || ((pNodeInfo->m_NodeState != kEplNmtMnuNodeStateResetConf)
                        && (pNodeInfo->m_NodeState != kEplNmtMnuNodeStateConfRestored)
                        && (pNodeInfo->m_NodeState != kEplNmtMnuNodeStateUnknown)))
                {
                    Ret = EplIdentuRequestIdentResponse(uiNodeId_p, NULL);
                }
                goto Exit;
            }

            case kEplNmtCmdStatusResponse:
            {   // issue request for remote node
                // if it is a non-existing node or operational and not async-only
                if (((pNodeInfo->m_dwNodeCfg
                     & (EPL_NODEASSIGN_NODE_IS_CN | EPL_NODEASSIGN_NODE_EXISTS))
                        != (EPL_NODEASSIGN_NODE_IS_CN | EPL_NODEASSIGN_NODE_EXISTS))
                    || (((pNodeInfo->m_dwNodeCfg & EPL_NODEASSIGN_ASYNCONLY_NODE) == 0)
                        && (pNodeInfo->m_NodeState == kEplNmtMnuNodeStateOperational)))
                {
                    Ret = EplStatusuRequestStatusResponse(uiNodeId_p, NULL);
                }
                goto Exit;
            }

            default:
            {
                break;
            }
        }
    }

    switch (NmtCommand_p)
    {
        case kEplNmtCmdResetNode:
        case kEplNmtCmdResetCommunication:
        case kEplNmtCmdResetConfiguration:
        case kEplNmtCmdSwReset:
        {
            if (uiNodeId_p == EPL_C_ADR_BROADCAST)
            {   // memorize that this is a user requested reset
                EplNmtMnuInstance_g.m_wFlags |= EPL_NMTMNU_FLAG_USER_RESET;
            }
            break;
        }

        case kEplNmtCmdStartNode:
        case kEplNmtCmdStopNode:
        case kEplNmtCmdEnterPreOperational2:
        case kEplNmtCmdEnableReadyToOperate:
        default:
        {
            break;
        }
/*
        case kEplNmtCmdInvalidService:
        default:
        {
            Ret = kEplObdAccessViolation;
            goto Exit;
        }
*/
    }

    // send command to remote node
    Ret = EplNmtMnuSendNmtCommand(uiNodeId_p, NmtCommand_p);

Exit:
    return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplNmtMnuTriggerStateChange
//
// Description: triggers the specified node command for the specified node.
//
// Parameters:  uiNodeId_p              = node ID for which the node command will be executed
//              NodeCommand_p           = node command
//
// Returns:     tEplKernel              = error code
//
// State:
//
//---------------------------------------------------------------------------

tEplKernel EplNmtMnuTriggerStateChange(unsigned int uiNodeId_p,
                                       tEplNmtNodeCommand  NodeCommand_p)
{
tEplKernel          Ret = kEplSuccessful;
tEplNmtMnuNodeCmd   NodeCmd;
tEplEvent           Event;

    if ((uiNodeId_p == 0) || (uiNodeId_p >= EPL_C_ADR_BROADCAST))
    {
        Ret = kEplInvalidNodeId;
        goto Exit;
    }

    NodeCmd.m_NodeCommand = NodeCommand_p;
    NodeCmd.m_uiNodeId = uiNodeId_p;
    Event.m_EventSink = kEplEventSinkNmtMnu;
    Event.m_EventType = kEplEventTypeNmtMnuNodeCmd;
    EPL_MEMSET(&Event.m_NetTime, 0x00, sizeof(Event.m_NetTime));
    Event.m_pArg = &NodeCmd;
    Event.m_uiSize = sizeof (NodeCmd);
    Ret = EplEventuPost(&Event);
    if (Ret != kEplSuccessful)
    {
        goto Exit;
    }

Exit:
    return Ret;
}


//---------------------------------------------------------------------------
//
// Function:    EplNmtMnuCbNmtStateChange
//
// Description: callback function for NMT state changes
//
// Parameters:  NmtStateChange_p        = NMT state change event
//
// Returns:     tEplKernel              = error code
//
//
// State:
//
//---------------------------------------------------------------------------

tEplKernel PUBLIC EplNmtMnuCbNmtStateChange(tEplEventNmtStateChange NmtStateChange_p)
{
tEplKernel      Ret = kEplSuccessful;

    // do work which must be done in that state
    switch (NmtStateChange_p.m_NewNmtState)
    {
        // EPL stack is not running
/*        case kEplNmtGsOff:
            break;

        // first init of the hardware
        case kEplNmtGsInitialising:
            break;

        // init of the manufacturer-specific profile area and the
        // standardised device profile area
        case kEplNmtGsResetApplication:
        {
            break;
        }

        // init of the communication profile area
        case kEplNmtGsResetCommunication:
        {
            break;
        }
*/
        // build the configuration with infos from OD
        case kEplNmtGsResetConfiguration:
        {
        DWORD           dwTimeout;
        tEplObdSize     ObdSize;

            // read object 0x1F80 NMT_StartUp_U32
            ObdSize = 4;
            Ret = EplObduReadEntry(0x1F80, 0, &EplNmtMnuInstance_g.m_dwNmtStartup, &ObdSize);
            if (Ret != kEplSuccessful)
            {
                break;
            }

            // compute StatusReqDelay = object 0x1006 * EPL_C_NMT_STATREQ_CYCLE
            ObdSize = sizeof (dwTimeout);
            Ret = EplObduReadEntry(0x1006, 0, &dwTimeout, &ObdSize);
            if (Ret != kEplSuccessful)
            {
                break;
            }
            if (dwTimeout != 0L)
            {
                EplNmtMnuInstance_g.m_ulStatusRequestDelay = dwTimeout * EPL_C_NMT_STATREQ_CYCLE / 1000L;
                if (EplNmtMnuInstance_g.m_ulStatusRequestDelay == 0L)
                {
                    EplNmtMnuInstance_g.m_ulStatusRequestDelay = 1L;    // at least 1 ms
                }

                // $$$ fetch and use MultiplexedCycleCount from OD
                EplNmtMnuInstance_g.m_ulTimeoutCheckCom = dwTimeout * EPL_C_NMT_STATREQ_CYCLE / 1000L;
                if (EplNmtMnuInstance_g.m_ulTimeoutCheckCom == 0L)
                {
                    EplNmtMnuInstance_g.m_ulTimeoutCheckCom = 1L;    // at least 1 ms
                }
            }

            // fetch ReadyToOp Timeout from OD
            ObdSize = sizeof (dwTimeout);
            Ret = EplObduReadEntry(0x1F89, 5, &dwTimeout, &ObdSize);
            if (Ret != kEplSuccessful)
            {
                break;
            }
            if (dwTimeout != 0L)
            {
                // convert [us] to [ms]
                dwTimeout /= 1000L;
                if (dwTimeout == 0L)
                {
                    dwTimeout = 1L;    // at least 1 ms
                }
                EplNmtMnuInstance_g.m_ulTimeoutReadyToOp = dwTimeout;
            }
            else
            {
                EplNmtMnuInstance_g.m_ulTimeoutReadyToOp = 0L;
            }
            break;
        }
/*
        //-----------------------------------------------------------
        // CN part of the state machine

        // node liste for EPL-Frames and check timeout
        case kEplNmtCsNotActive:
        {
            break;
        }

        // node process only async frames
        case kEplNmtCsPreOperational1:
        {
            break;
        }

        // node process isochronus and asynchronus frames
        case kEplNmtCsPreOperational2:
        {
            break;
        }

        // node should be configured und application is ready
        case kEplNmtCsReadyToOperate:
        {
            break;
        }

        // normal work state
        case kEplNmtCsOperational:
        {
            break;
        }

        // node stopped by MN
        // -> only process asynchronus frames
        case kEplNmtCsStopped:
        {
            break;
        }

        // no EPL cycle
        // -> normal ethernet communication
        case kEplNmtCsBasicEthernet:
        {
            break;
        }
*/
        //-----------------------------------------------------------
        // MN part of the state machine

        // node listens for EPL-Frames and check timeout
        case kEplNmtMsNotActive:
        {
            break;
        }

        // node processes only async frames
        case kEplNmtMsPreOperational1:
        {
        DWORD           dwTimeout;
        tEplTimerArg    TimerArg;
        tEplObdSize     ObdSize;
        tEplEvent       Event;
		BOOL			fNmtResetAllIssued = FALSE;

            // reset IdentResponses and running IdentRequests and StatusRequests
            Ret = EplIdentuReset();
            Ret = EplStatusuReset();

            // reset timers
            Ret = EplNmtMnuReset();

            // 2008/11/18 d.k. reset internal node info is not necessary,
            //                 because timer flags are important and other
            //                 things are reset by EplNmtMnuStartBootStep1().
/*
            EPL_MEMSET(EplNmtMnuInstance_g.m_aNodeInfo,
                       0,
                       sizeof (EplNmtMnuInstance_g.m_aNodeInfo));
*/

            // inform DLL about NMT state change,
            // so that it can clear the asynchonous queues and start the reduced cycle
            Event.m_EventSink = kEplEventSinkDllk;
            Event.m_EventType = kEplEventTypeDllkStartReducedCycle;
            EPL_MEMSET(&Event.m_NetTime, 0x00, sizeof(Event.m_NetTime));
            Event.m_pArg = NULL;
            Event.m_uiSize = 0;
            Ret = EplEventuPost(&Event);
            if (Ret != kEplSuccessful)
            {
                break;
            }

            // reset all nodes
            // skip this step if we come directly from OPERATIONAL
            // or it was just done before, e.g. because of a ResetNode command
            // from a diagnostic node
            if ((NmtStateChange_p.m_NmtEvent == kEplNmtEventTimerMsPreOp1)
                || ((EplNmtMnuInstance_g.m_wFlags & EPL_NMTMNU_FLAG_USER_RESET) == 0))
            {
                BENCHMARK_MOD_07_TOGGLE(7);

//                EPL_NMTMNU_DBG_POST_TRACE_VALUE(0,
//                                                EPL_C_ADR_BROADCAST,
//                                                kEplNmtCmdResetNode);

                Ret = EplNmtMnuSendNmtCommand(EPL_C_ADR_BROADCAST, kEplNmtCmdResetNode);
                if (Ret != kEplSuccessful)
                {
                    break;
                }
				fNmtResetAllIssued = TRUE;
            }

            // clear global flags, e.g. reenable boot process
            EplNmtMnuInstance_g.m_wFlags = 0;

            // start network scan
            Ret = EplNmtMnuStartBootStep1(fNmtResetAllIssued);

            // start timer for 0x1F89/2 MNTimeoutPreOp1_U32
            ObdSize = sizeof (dwTimeout);
            Ret = EplObduReadEntry(0x1F89, 2, &dwTimeout, &ObdSize);
            if (Ret != kEplSuccessful)
            {
                break;
            }
            if (dwTimeout != 0L)
            {
                dwTimeout /= 1000L;
                if (dwTimeout == 0L)
                {
                    dwTimeout = 1L; // at least 1 ms
                }
                TimerArg.m_EventSink = kEplEventSinkNmtMnu;
                TimerArg.m_Arg.m_dwVal = 0;
                Ret = EplTimeruModifyTimerMs(&EplNmtMnuInstance_g.m_TimerHdlNmtState, dwTimeout, TimerArg);
            }
            break;
        }

        // node processes isochronous and asynchronous frames
        case kEplNmtMsPreOperational2:
        {
            // add identified CNs to isochronous phase
            // send EnableReadyToOp to all identified CNs
            Ret = EplNmtMnuStartBootStep2();

            // wait for NMT state change of CNs
            break;
        }

        // node should be configured und application is ready
        case kEplNmtMsReadyToOperate:
        {
            // check if PRes of CNs are OK
            // d.k. that means wait CycleLength * MultiplexCycleCount (i.e. start timer)
            //      because Dllk checks PRes of CNs automatically in ReadyToOp
            Ret = EplNmtMnuStartCheckCom();
            break;
        }

        // normal work state
        case kEplNmtMsOperational:
        {
            // send StartNode to CNs
            // wait for NMT state change of CNs
            Ret = EplNmtMnuStartNodes();
            break;
        }

        // no EPL cycle
        // -> normal ethernet communication
        case kEplNmtMsBasicEthernet:
        {
            break;
        }

        default:
        {
//            TRACE0("EplNmtMnuCbNmtStateChange(): unhandled NMT state\n");
        }
    }

    return Ret;
}


//---------------------------------------------------------------------------
//
// Function:    EplNmtMnuCbCheckEvent
//
// Description: callback funktion for NMT events before they are actually executed.
//              The EPL API layer must forward NMT events from NmtCnu module.
//              This module will reject some NMT commands while MN.
//
// Parameters:  NmtEvent_p              = outstanding NMT event for approval
//
// Returns:     tEplKernel              = error code
//                      kEplReject      = reject the NMT event
//
// State:
//
//---------------------------------------------------------------------------

tEplKernel PUBLIC EplNmtMnuCbCheckEvent(tEplNmtEvent NmtEvent_p)
{
tEplKernel      Ret = kEplSuccessful;

    UNUSED_PARAMETER(NmtEvent_p);

    return Ret;
}


//---------------------------------------------------------------------------
//
// Function:    EplNmtuProcessEvent
//
// Description: processes events from event queue
//
// Parameters:  pEvent_p        = pointer to event
//
// Returns:     tEplKernel      = errorcode
//
// State:
//
//---------------------------------------------------------------------------

EPLDLLEXPORT tEplKernel PUBLIC EplNmtMnuProcessEvent(
            tEplEvent* pEvent_p)
{
tEplKernel      Ret;

    Ret = kEplSuccessful;

    // process event
    switch(pEvent_p->m_EventType)
    {
        // timer event
        case kEplEventTypeTimer:
        {
        tEplTimerEventArg*  pTimerEventArg = (tEplTimerEventArg*)pEvent_p->m_pArg;
        unsigned int        uiNodeId;

            uiNodeId = (unsigned int) (pTimerEventArg->m_Arg.m_dwVal & EPL_NMTMNU_TIMERARG_NODE_MASK);
            if (uiNodeId != 0)
            {
            tEplObdSize         ObdSize;
            BYTE                bNmtState;
            tEplNmtMnuNodeInfo* pNodeInfo;

                pNodeInfo = EPL_NMTMNU_GET_NODEINFO(uiNodeId);

                ObdSize = 1;
                Ret = EplObduReadEntry(0x1F8E, uiNodeId, &bNmtState, &ObdSize);
                if (Ret != kEplSuccessful)
                {
                    break;
                }

                if ((pTimerEventArg->m_Arg.m_dwVal & EPL_NMTMNU_TIMERARG_IDENTREQ) != 0L)
                {
                    if ((DWORD)(pNodeInfo->m_wFlags & EPL_NMTMNU_NODE_FLAG_COUNT_STATREQ)
                        != (pTimerEventArg->m_Arg.m_dwVal & EPL_NMTMNU_TIMERARG_COUNT_SR))
                    {   // this is an old (already deleted or modified) timer
                        // but not the current timer
                        // so discard it
//                        EPL_NMTMNU_DBG_POST_TRACE_VALUE(kEplNmtMnuIntNodeEventTimerIdentReq,
//                                                        uiNodeId,
//                                                        ((pNodeInfo->m_NodeState << 8)
//                                                         | 0xFF));

                        break;
                    }
/*
                    EPL_NMTMNU_DBG_POST_TRACE_VALUE(kEplNmtMnuIntNodeEventTimerIdentReq,
                                                    uiNodeId,
                                                    ((pNodeInfo->m_NodeState << 8)
                                                     | 0x80
                                                     | ((pNodeInfo->m_wFlags & EPL_NMTMNU_NODE_FLAG_COUNT_STATREQ) >> 6)
                                                     | ((pTimerEventArg->m_Arg.m_dwVal & EPL_NMTMNU_TIMERARG_COUNT_SR) >> 8)));
*/
                    Ret = EplNmtMnuProcessInternalEvent(uiNodeId,
                                                        (tEplNmtState) (bNmtState | EPL_NMT_TYPE_CS),
                                                        EPL_E_NO_ERROR,
                                                        kEplNmtMnuIntNodeEventTimerIdentReq);
                }

                else if ((pTimerEventArg->m_Arg.m_dwVal & EPL_NMTMNU_TIMERARG_STATREQ) != 0L)
                {
                    if ((DWORD)(pNodeInfo->m_wFlags & EPL_NMTMNU_NODE_FLAG_COUNT_STATREQ)
                        != (pTimerEventArg->m_Arg.m_dwVal & EPL_NMTMNU_TIMERARG_COUNT_SR))
                    {   // this is an old (already deleted or modified) timer
                        // but not the current timer
                        // so discard it
//                       EPL_NMTMNU_DBG_POST_TRACE_VALUE(kEplNmtMnuIntNodeEventTimerStatReq,
//                                                        uiNodeId,
//                                                        ((pNodeInfo->m_NodeState << 8)
//                                                         | 0xFF));

                        break;
                    }
/*
                    EPL_NMTMNU_DBG_POST_TRACE_VALUE(kEplNmtMnuIntNodeEventTimerStatReq,
                                                    uiNodeId,
                                                    ((pNodeInfo->m_NodeState << 8)
                                                     | 0x80
                                                     | ((pNodeInfo->m_wFlags & EPL_NMTMNU_NODE_FLAG_COUNT_STATREQ) >> 6)
                                                     | ((pTimerEventArg->m_Arg.m_dwVal & EPL_NMTMNU_TIMERARG_COUNT_SR) >> 8)));
*/
                    Ret = EplNmtMnuProcessInternalEvent(uiNodeId,
                                                        (tEplNmtState) (bNmtState | EPL_NMT_TYPE_CS),
                                                        EPL_E_NO_ERROR,
                                                        kEplNmtMnuIntNodeEventTimerStatReq);
                }

                else if ((pTimerEventArg->m_Arg.m_dwVal & EPL_NMTMNU_TIMERARG_STATE_MON) != 0L)
                {
                    if ((DWORD)(pNodeInfo->m_wFlags & EPL_NMTMNU_NODE_FLAG_COUNT_STATREQ)
                        != (pTimerEventArg->m_Arg.m_dwVal & EPL_NMTMNU_TIMERARG_COUNT_SR))
                    {   // this is an old (already deleted or modified) timer
                        // but not the current timer
                        // so discard it
//                        EPL_NMTMNU_DBG_POST_TRACE_VALUE(kEplNmtMnuIntNodeEventTimerStateMon,
//                                                        uiNodeId,
//                                                        ((pNodeInfo->m_NodeState << 8)
//                                                         | 0xFF));

                        break;
                    }
/*
                    EPL_NMTMNU_DBG_POST_TRACE_VALUE(kEplNmtMnuIntNodeEventTimerStatReq,
                                                    uiNodeId,
                                                    ((pNodeInfo->m_NodeState << 8)
                                                     | 0x80
                                                     | ((pNodeInfo->m_wFlags & EPL_NMTMNU_NODE_FLAG_COUNT_STATREQ) >> 6)
                                                     | ((pTimerEventArg->m_Arg.m_dwVal & EPL_NMTMNU_TIMERARG_COUNT_SR) >> 8)));
*/
                    Ret = EplNmtMnuProcessInternalEvent(uiNodeId,
                                                        (tEplNmtState) (bNmtState | EPL_NMT_TYPE_CS),
                                                        EPL_E_NO_ERROR,
                                                        kEplNmtMnuIntNodeEventTimerStateMon);
                }

                else if ((pTimerEventArg->m_Arg.m_dwVal & EPL_NMTMNU_TIMERARG_LONGER) != 0L)
                {
                    if ((DWORD)(pNodeInfo->m_wFlags & EPL_NMTMNU_NODE_FLAG_COUNT_LONGER)
                        != (pTimerEventArg->m_Arg.m_dwVal & EPL_NMTMNU_TIMERARG_COUNT_LO))
                    {   // this is an old (already deleted or modified) timer
                        // but not the current timer
                        // so discard it
//                        EPL_NMTMNU_DBG_POST_TRACE_VALUE(kEplNmtMnuIntNodeEventTimerLonger,
//                                                        uiNodeId,
//                                                        ((pNodeInfo->m_NodeState << 8)
//                                                         | 0xFF));

                        break;
                    }
/*
                    EPL_NMTMNU_DBG_POST_TRACE_VALUE(kEplNmtMnuIntNodeEventTimerLonger,
                                                    uiNodeId,
                                                    ((pNodeInfo->m_NodeState << 8)
                                                     | 0x80
                                                     | ((pNodeInfo->m_wFlags & EPL_NMTMNU_NODE_FLAG_COUNT_LONGER) >> 6)
                                                     | ((pTimerEventArg->m_Arg.m_dwVal & EPL_NMTMNU_TIMERARG_COUNT_LO) >> 8)));
*/
                    Ret = EplNmtMnuProcessInternalEvent(uiNodeId,
                                                        (tEplNmtState) (bNmtState | EPL_NMT_TYPE_CS),
                                                        EPL_E_NO_ERROR,
                                                        kEplNmtMnuIntNodeEventTimerLonger);
                }

            }
            else
            {   // global timer event
            }
            break;
        }

        case kEplEventTypeHeartbeat:
        {
        tEplHeartbeatEvent* pHeartbeatEvent = (tEplHeartbeatEvent*)pEvent_p->m_pArg;

            Ret = EplNmtMnuProcessInternalEvent(pHeartbeatEvent->m_uiNodeId,
                                                pHeartbeatEvent->m_NmtState,
                                                pHeartbeatEvent->m_wErrorCode,
                                                kEplNmtMnuIntNodeEventHeartbeat);
            break;
        }

        case kEplEventTypeNmtMnuNmtCmdSent:
        {
        tEplFrame* pFrame = (tEplFrame*)pEvent_p->m_pArg;
        unsigned int        uiNodeId;
        tEplNmtCommand      NmtCommand;
        BYTE                bNmtState;

            uiNodeId = AmiGetByteFromLe(&pFrame->m_le_bDstNodeId);
            NmtCommand = (tEplNmtCommand) AmiGetByteFromLe(&pFrame->m_Data.m_Asnd.m_Payload.m_NmtCommandService.m_le_bNmtCommandId);

            switch (NmtCommand)
            {
                case kEplNmtCmdStartNode:
                    bNmtState = (BYTE) (kEplNmtCsOperational & 0xFF);
                    break;

                case kEplNmtCmdStopNode:
                    bNmtState = (BYTE) (kEplNmtCsStopped & 0xFF);
                    break;

                case kEplNmtCmdEnterPreOperational2:
                    bNmtState = (BYTE) (kEplNmtCsPreOperational2 & 0xFF);
                    break;

                case kEplNmtCmdEnableReadyToOperate:
                    // d.k. do not change expected node state, because of DS 1.0.0 7.3.1.2.1 Plain NMT State Command
                    //      and because node may not change NMT state within EPL_C_NMT_STATE_TOLERANCE
                    bNmtState = (BYTE) (kEplNmtCsPreOperational2 & 0xFF);
                    break;

                case kEplNmtCmdResetNode:
                case kEplNmtCmdResetCommunication:
                case kEplNmtCmdResetConfiguration:
                case kEplNmtCmdSwReset:
                    bNmtState = (BYTE) (kEplNmtCsNotActive & 0xFF);
                    // EplNmtMnuProcessInternalEvent() sets internal node state to kEplNmtMnuNodeStateUnknown
                    // after next unresponded IdentRequest/StatusRequest
                    break;

                default:
                    goto Exit;
            }

            // process as internal event which update expected NMT state in OD
            if (uiNodeId != EPL_C_ADR_BROADCAST)
            {
                Ret = EplNmtMnuProcessInternalEvent(uiNodeId,
                                                    (tEplNmtState) (bNmtState | EPL_NMT_TYPE_CS),
                                                    0,
                                                    kEplNmtMnuIntNodeEventNmtCmdSent);

            }
            else
            {   // process internal event for all active nodes (except myself)

                for (uiNodeId = 1; uiNodeId <= tabentries(EplNmtMnuInstance_g.m_aNodeInfo); uiNodeId++)
                {
                    if ((EPL_NMTMNU_GET_NODEINFO(uiNodeId)->m_dwNodeCfg & (EPL_NODEASSIGN_NODE_IS_CN | EPL_NODEASSIGN_NODE_EXISTS)) != 0)
                    {
                        Ret = EplNmtMnuProcessInternalEvent(uiNodeId,
                                                            (tEplNmtState) (bNmtState | EPL_NMT_TYPE_CS),
                                                            0,
                                                            kEplNmtMnuIntNodeEventNmtCmdSent);

                        if (Ret != kEplSuccessful)
                        {
                            goto Exit;
                        }
                    }
                }

                if ((EplNmtMnuInstance_g.m_wFlags & EPL_NMTMNU_FLAG_USER_RESET) != 0)
                {   // user or diagnostic nodes requests a reset of the MN
                tEplNmtEvent    NmtEvent;

                    switch (NmtCommand)
                    {
                        case kEplNmtCmdResetNode:
                        {
                            NmtEvent = kEplNmtEventResetNode;
                            break;
                        }

                        case kEplNmtCmdResetCommunication:
                        {
                            NmtEvent = kEplNmtEventResetCom;
                            break;
                        }

                        case kEplNmtCmdResetConfiguration:
                        {
                            NmtEvent = kEplNmtEventResetConfig;
                            break;
                        }

                        case kEplNmtCmdSwReset:
                        {
                            NmtEvent = kEplNmtEventSwReset;
                            break;
                        }

                        case kEplNmtCmdInvalidService:
                        default:
                        {   // actually no reset was requested
                            goto Exit;
                        }
                    }

                    Ret = EplNmtuNmtEvent(NmtEvent);
                    if (Ret != kEplSuccessful)
                    {
                        goto Exit;
                    }
                }
            }

            break;
        }

        case kEplEventTypeNmtMnuNodeCmd:
        {
        tEplNmtMnuNodeCmd*      pNodeCmd = (tEplNmtMnuNodeCmd*)pEvent_p->m_pArg;
        tEplNmtMnuIntNodeEvent  NodeEvent;
        tEplObdSize             ObdSize;
        BYTE                    bNmtState;
        WORD                    wErrorCode = EPL_E_NO_ERROR;

            if ((pNodeCmd->m_uiNodeId == 0) || (pNodeCmd->m_uiNodeId >= EPL_C_ADR_BROADCAST))
            {
                Ret = kEplInvalidNodeId;
                goto Exit;
            }

            switch (pNodeCmd->m_NodeCommand)
            {
                case kEplNmtNodeCommandBoot:
                {
                    NodeEvent = kEplNmtMnuIntNodeEventBoot;
                    break;
                }

                case kEplNmtNodeCommandConfOk:
                {
                    NodeEvent = kEplNmtMnuIntNodeEventConfigured;
                    break;
                }

                case kEplNmtNodeCommandConfErr:
                {
                    NodeEvent = kEplNmtMnuIntNodeEventError;
                    wErrorCode = EPL_E_NMT_BPO1_CF_VERIFY;
                    break;
                }

                case kEplNmtNodeCommandConfRestored:
                {
                    NodeEvent = kEplNmtMnuIntNodeEventExecResetNode;
                    break;
                }

                case kEplNmtNodeCommandConfReset:
                {
                    NodeEvent = kEplNmtMnuIntNodeEventExecResetConf;
                    break;
                }

                default:
                {   // invalid node command
                    goto Exit;
                }
            }

            // fetch current NMT state
            ObdSize = sizeof (bNmtState);
            Ret = EplObduReadEntry(0x1F8E, pNodeCmd->m_uiNodeId, &bNmtState, &ObdSize);
            if (Ret != kEplSuccessful)
            {
                goto Exit;
            }

            Ret = EplNmtMnuProcessInternalEvent(pNodeCmd->m_uiNodeId,
                                                (tEplNmtState) (bNmtState | EPL_NMT_TYPE_CS),
                                                wErrorCode,
                                                NodeEvent);
            break;
        }

        default:
        {
            Ret = kEplNmtInvalidEvent;
        }

    }

Exit:
    return Ret;
}


//---------------------------------------------------------------------------
//
// Function:    EplNmtMnuGetRunningTimerStatReq
//
// Description: returns a bit field with running StatReq timers
//              just for debugging purposes
//
// Parameters:  (none)
//
// Returns:     tEplKernel              = error code
//
// State:
//
//---------------------------------------------------------------------------

tEplKernel PUBLIC EplNmtMnuGetDiagnosticInfo(unsigned int* puiMandatorySlaveCount_p,
                                             unsigned int* puiSignalSlaveCount_p,
                                             WORD* pwFlags_p)
{
tEplKernel      Ret = kEplSuccessful;

    if ((puiMandatorySlaveCount_p == NULL)
        || (puiSignalSlaveCount_p == NULL)
        || (pwFlags_p == NULL))
    {
        Ret = kEplNmtInvalidParam;
        goto Exit;
    }

    *puiMandatorySlaveCount_p = EplNmtMnuInstance_g.m_uiMandatorySlaveCount;
    *puiSignalSlaveCount_p = EplNmtMnuInstance_g.m_uiSignalSlaveCount;
    *pwFlags_p = EplNmtMnuInstance_g.m_wFlags;

Exit:
    return Ret;
}


//---------------------------------------------------------------------------
//
// Function:    EplNmtMnuGetRunningTimerStatReq
//
// Description: returns a bit field with running StatReq timers
//              just for debugging purposes
//
// Parameters:  (none)
//
// Returns:     tEplKernel              = error code
//
// State:
//
//---------------------------------------------------------------------------
/*
DWORD EplNmtMnuGetRunningTimerStatReq(void)
{
tEplKernel      Ret = kEplSuccessful;
unsigned int    uiIndex;
tEplNmtMnuNodeInfo* pNodeInfo;

    pNodeInfo = EplNmtMnuInstance_g.m_aNodeInfo;
    for (uiIndex = 1; uiIndex <= tabentries(EplNmtMnuInstance_g.m_aNodeInfo); uiIndex++, pNodeInfo++)
    {
        if (pNodeInfo->m_NodeState == kEplNmtMnuNodeStateConfigured)
        {
            // reset flag "scanned once"
            pNodeInfo->m_wFlags &= ~EPL_NMTMNU_NODE_FLAG_SCANNED;

            Ret = EplNmtMnuNodeBootStep2(uiIndex, pNodeInfo);
            if (Ret != kEplSuccessful)
            {
                goto Exit;
            }
            EplNmtMnuInstance_g.m_uiSignalSlaveCount++;
            // signal slave counter shall be decremented if StatusRequest was sent once to a CN
            // mandatory slave counter shall be decremented if mandatory CN is ReadyToOp
        }
    }

Exit:
    return Ret;
}
*/

//=========================================================================//
//                                                                         //
//          P R I V A T E   F U N C T I O N S                              //
//                                                                         //
//=========================================================================//

//---------------------------------------------------------------------------
//
// Function:    EplNmtMnuCbNmtRequest
//
// Description: callback funktion for NmtRequest
//
// Parameters:  pFrameInfo_p            = Frame with the NmtRequest
//
// Returns:     tEplKernel              = error code
//
// State:
//
//---------------------------------------------------------------------------

static tEplKernel PUBLIC EplNmtMnuCbNmtRequest(tEplFrameInfo * pFrameInfo_p)
{
tEplKernel      Ret = kEplSuccessful;
unsigned int    uiTargetNodeId;
tEplNmtCommand  NmtCommand;
tEplNmtRequestService*  pNmtRequestService;

    if ((pFrameInfo_p == NULL)
        || (pFrameInfo_p->m_pFrame == NULL))
    {
        Ret = kEplNmtInvalidFramePointer;
        goto Exit;
    }

    pNmtRequestService = &pFrameInfo_p->m_pFrame->m_Data.m_Asnd.m_Payload.m_NmtRequestService;

    NmtCommand = (tEplNmtCommand)AmiGetByteFromLe(
            &pNmtRequestService->m_le_bNmtCommandId);

    uiTargetNodeId = AmiGetByteFromLe(
            &pNmtRequestService->m_le_bTargetNodeId);

    Ret = EplNmtMnuRequestNmtCommand(uiTargetNodeId,
                                     NmtCommand);
    if (Ret != kEplSuccessful)
    {   // error -> reply with kEplNmtCmdInvalidService
    unsigned int uiSourceNodeId;

        uiSourceNodeId = AmiGetByteFromLe(
                &pFrameInfo_p->m_pFrame->m_le_bSrcNodeId);
        Ret = EplNmtMnuSendNmtCommand(uiSourceNodeId, kEplNmtCmdInvalidService);
    }

Exit:
    return Ret;
}


//---------------------------------------------------------------------------
//
// Function:    EplNmtMnuCbIdentResponse
//
// Description: callback funktion for IdentResponse
//
// Parameters:  uiNodeId_p              = node ID for which IdentReponse was received
//              pIdentResponse_p        = pointer to IdentResponse
//                                        is NULL if node did not answer
//
// Returns:     tEplKernel              = error code
//
// State:
//
//---------------------------------------------------------------------------

static tEplKernel PUBLIC EplNmtMnuCbIdentResponse(
                                  unsigned int        uiNodeId_p,
                                  tEplIdentResponse* pIdentResponse_p)
{
tEplKernel      Ret = kEplSuccessful;

    if (pIdentResponse_p == NULL)
    {   // node did not answer
        Ret = EplNmtMnuProcessInternalEvent(uiNodeId_p,
                                            kEplNmtCsNotActive,
                                            EPL_E_NMT_NO_IDENT_RES, // was EPL_E_NO_ERROR
                                            kEplNmtMnuIntNodeEventNoIdentResponse);
    }
    else
    {   // node answered IdentRequest
    tEplObdSize ObdSize;
    DWORD       dwDevType;
    WORD        wErrorCode = EPL_E_NO_ERROR;
    tEplNmtState NmtState = (tEplNmtState) (AmiGetByteFromLe(&pIdentResponse_p->m_le_bNmtStatus) | EPL_NMT_TYPE_CS);

        // check IdentResponse $$$ move to ProcessIntern, because this function may be called also if CN

        // check DeviceType (0x1F84)
        ObdSize = 4;
        Ret = EplObduReadEntry(0x1F84, uiNodeId_p, &dwDevType, &ObdSize);
        if (Ret != kEplSuccessful)
        {
            goto Exit;
        }
        if (dwDevType != 0L)
        {   // actually compare it with DeviceType from IdentResponse
            if (AmiGetDwordFromLe(&pIdentResponse_p->m_le_dwDeviceType) != dwDevType)
            {   // wrong DeviceType
                NmtState = kEplNmtCsNotActive;
                wErrorCode = EPL_E_NMT_BPO1_DEVICE_TYPE;
            }
        }

        Ret = EplNmtMnuProcessInternalEvent(uiNodeId_p,
                                            NmtState,
                                            wErrorCode,
                                            kEplNmtMnuIntNodeEventIdentResponse);
    }

Exit:
    return Ret;
}


//---------------------------------------------------------------------------
//
// Function:    EplNmtMnuCbStatusResponse
//
// Description: callback funktion for StatusResponse
//
// Parameters:  uiNodeId_p              = node ID for which IdentReponse was received
//              pIdentResponse_p        = pointer to IdentResponse
//                                        is NULL if node did not answer
//
// Returns:     tEplKernel              = error code
//
// State:
//
//---------------------------------------------------------------------------

static tEplKernel PUBLIC EplNmtMnuCbStatusResponse(
                                  unsigned int        uiNodeId_p,
                                  tEplStatusResponse* pStatusResponse_p)
{
tEplKernel      Ret = kEplSuccessful;

    if (pStatusResponse_p == NULL)
    {   // node did not answer
        Ret = EplNmtMnuProcessInternalEvent(uiNodeId_p,
                                            kEplNmtCsNotActive,
                                            EPL_E_NMT_NO_STATUS_RES, // was EPL_E_NO_ERROR
                                            kEplNmtMnuIntNodeEventNoStatusResponse);
    }
    else
    {   // node answered StatusRequest
        Ret = EplNmtMnuProcessInternalEvent(uiNodeId_p,
                                            (tEplNmtState) (AmiGetByteFromLe(&pStatusResponse_p->m_le_bNmtStatus) | EPL_NMT_TYPE_CS),
                                            EPL_E_NO_ERROR,
                                            kEplNmtMnuIntNodeEventStatusResponse);
    }

    return Ret;
}


//---------------------------------------------------------------------------
//
// Function:    EplNmtMnuStartBootStep1
//
// Description: starts BootStep1
//
// Parameters:  (none)
//
// Returns:     tEplKernel              = error code
//
// State:
//
//---------------------------------------------------------------------------

static tEplKernel EplNmtMnuStartBootStep1(BOOL fNmtResetAllIssued_p)
{
tEplKernel			Ret = kEplSuccessful;
unsigned int		uiSubIndex;
unsigned int		uiLocalNodeId;
DWORD				dwNodeCfg;
tEplObdSize			ObdSize;
tEplNmtMnuNodeInfo*	pNodeInfo;

    // $$$ d.k.: save current time for 0x1F89/2 MNTimeoutPreOp1_U32

    // start network scan
    EplNmtMnuInstance_g.m_uiMandatorySlaveCount = 0;
    EplNmtMnuInstance_g.m_uiSignalSlaveCount = 0;
    // check 0x1F81
    uiLocalNodeId = EplObduGetNodeId();
    for (uiSubIndex = 1; uiSubIndex <= 254; uiSubIndex++)
    {
        ObdSize = 4;
        Ret = EplObduReadEntry(0x1F81, uiSubIndex, &dwNodeCfg, &ObdSize);
        if (Ret != kEplSuccessful)
        {
            goto Exit;
        }
        if (uiSubIndex != uiLocalNodeId)
        {
			pNodeInfo = EPL_NMTMNU_GET_NODEINFO(uiSubIndex);

            // reset flags "not scanned" and "isochronous"
            pNodeInfo->m_wFlags &= ~(EPL_NMTMNU_NODE_FLAG_ISOCHRON | EPL_NMTMNU_NODE_FLAG_NOT_SCANNED);

            if (uiSubIndex == EPL_C_ADR_DIAG_DEF_NODE_ID)
            {   // diagnostic node must be scanned by MN in any case
                dwNodeCfg |= (EPL_NODEASSIGN_NODE_IS_CN | EPL_NODEASSIGN_NODE_EXISTS);
                // and it must be isochronously accessed
                dwNodeCfg &= ~EPL_NODEASSIGN_ASYNCONLY_NODE;
            }

            // save node config in local node info structure
            pNodeInfo->m_dwNodeCfg = dwNodeCfg;
            pNodeInfo->m_NodeState = kEplNmtMnuNodeStateUnknown;

            if ((dwNodeCfg & (EPL_NODEASSIGN_NODE_IS_CN | EPL_NODEASSIGN_NODE_EXISTS)) != 0)
            {   // node is configured as CN

				if (fNmtResetAllIssued_p == FALSE)
				{
					// identify the node
	                Ret = EplIdentuRequestIdentResponse(uiSubIndex, EplNmtMnuCbIdentResponse);
					if (Ret != kEplSuccessful)
					{
						goto Exit;
					}
				}

                // set flag "not scanned"
                pNodeInfo->m_wFlags |= EPL_NMTMNU_NODE_FLAG_NOT_SCANNED;
                EplNmtMnuInstance_g.m_uiSignalSlaveCount++;
                // signal slave counter shall be decremented if IdentRequest was sent once to a CN

                if ((dwNodeCfg & EPL_NODEASSIGN_MANDATORY_CN) != 0)
                {   // node is a mandatory CN
                    EplNmtMnuInstance_g.m_uiMandatorySlaveCount++;
                    // mandatory slave counter shall be decremented if mandatory CN was configured successfully
                }
            }
        }
        else
        {   // subindex of MN
            if ((dwNodeCfg & (EPL_NODEASSIGN_MN_PRES | EPL_NODEASSIGN_NODE_EXISTS)) != 0)
            {   // MN shall send PRes
            tEplDllNodeOpParam  NodeOpParam;

                NodeOpParam.m_OpNodeType = kEplDllNodeOpTypeIsochronous;
                NodeOpParam.m_uiNodeId = uiLocalNodeId;

                Ret = EplDlluCalAddNode(&NodeOpParam);
            }
        }
    }

Exit:
    return Ret;
}


//---------------------------------------------------------------------------
//
// Function:    EplNmtMnuStartBootStep2
//
// Description: starts BootStep2.
//              That means add nodes to isochronous phase and send
//              NMT EnableReadyToOp.
//
// Parameters:  (none)
//
// Returns:     tEplKernel              = error code
//
// State:
//
//---------------------------------------------------------------------------

static tEplKernel EplNmtMnuStartBootStep2(void)
{
tEplKernel      Ret = kEplSuccessful;
unsigned int    uiIndex;
tEplNmtMnuNodeInfo* pNodeInfo;


    if ((EplNmtMnuInstance_g.m_wFlags & EPL_NMTMNU_FLAG_HALTED) == 0)
    {   // boot process is not halted
        // add nodes to isochronous phase and send NMT EnableReadyToOp
        EplNmtMnuInstance_g.m_uiMandatorySlaveCount = 0;
        EplNmtMnuInstance_g.m_uiSignalSlaveCount = 0;
        // reset flag that application was informed about possible state change
        EplNmtMnuInstance_g.m_wFlags &= ~EPL_NMTMNU_FLAG_APP_INFORMED;

        pNodeInfo = EplNmtMnuInstance_g.m_aNodeInfo;
        for (uiIndex = 1; uiIndex <= tabentries(EplNmtMnuInstance_g.m_aNodeInfo); uiIndex++, pNodeInfo++)
        {
            if (pNodeInfo->m_NodeState == kEplNmtMnuNodeStateConfigured)
            {
                Ret = EplNmtMnuNodeBootStep2(uiIndex, pNodeInfo);
                if (Ret != kEplSuccessful)
                {
                    goto Exit;
                }

                // set flag "not scanned"
                pNodeInfo->m_wFlags |= EPL_NMTMNU_NODE_FLAG_NOT_SCANNED;

                EplNmtMnuInstance_g.m_uiSignalSlaveCount++;
                // signal slave counter shall be decremented if StatusRequest was sent once to a CN

                if ((pNodeInfo->m_dwNodeCfg & EPL_NODEASSIGN_MANDATORY_CN) != 0)
                {   // node is a mandatory CN
                    EplNmtMnuInstance_g.m_uiMandatorySlaveCount++;
                }

                // mandatory slave counter shall be decremented if mandatory CN is ReadyToOp
            }
        }
    }

Exit:
    return Ret;
}


//---------------------------------------------------------------------------
//
// Function:    EplNmtMnuNodeBootStep2
//
// Description: starts BootStep2 for the specified node.
//              This means the CN is added to isochronous phase if not
//              async-only and it gets the NMT command EnableReadyToOp.
//              The CN must be in node state Configured, when it enters
//              BootStep2. When BootStep2 finishes, the CN is in node state
//              ReadyToOp.
//              If TimeoutReadyToOp in object 0x1F89/5 is configured,
//              TimerHdlLonger will be started with this timeout.
//
// Parameters:  uiNodeId_p              = node ID
//              pNodeInfo_p             = pointer to internal node info structure
//
// Returns:     tEplKernel              = error code
//
// State:
//
//---------------------------------------------------------------------------

static tEplKernel EplNmtMnuNodeBootStep2(unsigned int uiNodeId_p, tEplNmtMnuNodeInfo* pNodeInfo_p)
{
tEplKernel      Ret = kEplSuccessful;
tEplDllNodeOpParam  NodeOpParam;
DWORD           dwNodeCfg;
tEplTimerArg    TimerArg;

    dwNodeCfg = pNodeInfo_p->m_dwNodeCfg;
    if ((dwNodeCfg & EPL_NODEASSIGN_ASYNCONLY_NODE) == 0)
    {   // add node to isochronous phase
        NodeOpParam.m_OpNodeType = kEplDllNodeOpTypeIsochronous;
        NodeOpParam.m_uiNodeId = uiNodeId_p;

        pNodeInfo_p->m_wFlags |= EPL_NMTMNU_NODE_FLAG_ISOCHRON;

        Ret = EplDlluCalAddNode(&NodeOpParam);
        if (Ret != kEplSuccessful)
        {
            goto Exit;
        }

    }
//
//    EPL_NMTMNU_DBG_POST_TRACE_VALUE(0,
//                                    uiNodeId_p,
//                                    kEplNmtCmdEnableReadyToOperate);

    Ret = EplNmtMnuSendNmtCommand(uiNodeId_p, kEplNmtCmdEnableReadyToOperate);
    if (Ret != kEplSuccessful)
    {
        goto Exit;
    }

    if (EplNmtMnuInstance_g.m_ulTimeoutReadyToOp != 0L)
    {   // start timer
        // when the timer expires the CN must be ReadyToOp
        EPL_NMTMNU_SET_FLAGS_TIMERARG_LONGER(
                pNodeInfo_p, uiNodeId_p, TimerArg);
//        TimerArg.m_EventSink = kEplEventSinkNmtMnu;
//        TimerArg.m_Arg.m_dwVal = EPL_NMTMNU_TIMERARG_LONGER | uiNodeId_p;
        Ret = EplTimeruModifyTimerMs(&pNodeInfo_p->m_TimerHdlLonger, EplNmtMnuInstance_g.m_ulTimeoutReadyToOp, TimerArg);
    }

Exit:
    return Ret;
}


//---------------------------------------------------------------------------
//
// Function:    EplNmtMnuStartCheckCom
//
// Description: starts CheckCommunication
//
// Parameters:  (none)
//
// Returns:     tEplKernel              = error code
//
// State:
//
//---------------------------------------------------------------------------

static tEplKernel EplNmtMnuStartCheckCom(void)
{
tEplKernel      Ret = kEplSuccessful;
unsigned int    uiIndex;
tEplNmtMnuNodeInfo* pNodeInfo;


    if ((EplNmtMnuInstance_g.m_wFlags & EPL_NMTMNU_FLAG_HALTED) == 0)
    {   // boot process is not halted
        // wait some time and check that no communication error occurs
        EplNmtMnuInstance_g.m_uiMandatorySlaveCount = 0;
        EplNmtMnuInstance_g.m_uiSignalSlaveCount = 0;
        // reset flag that application was informed about possible state change
        EplNmtMnuInstance_g.m_wFlags &= ~EPL_NMTMNU_FLAG_APP_INFORMED;

        pNodeInfo = EplNmtMnuInstance_g.m_aNodeInfo;
        for (uiIndex = 1; uiIndex <= tabentries(EplNmtMnuInstance_g.m_aNodeInfo); uiIndex++, pNodeInfo++)
        {
            if (pNodeInfo->m_NodeState == kEplNmtMnuNodeStateReadyToOp)
            {
                Ret = EplNmtMnuNodeCheckCom(uiIndex, pNodeInfo);
                if (Ret == kEplReject)
                {   // timer was started
                    // wait until it expires
                    if ((pNodeInfo->m_dwNodeCfg & EPL_NODEASSIGN_MANDATORY_CN) != 0)
                    {   // node is a mandatory CN
                        EplNmtMnuInstance_g.m_uiMandatorySlaveCount++;
                    }
                }
                else if (Ret != kEplSuccessful)
                {
                    goto Exit;
                }

                // set flag "not scanned"
                pNodeInfo->m_wFlags |= EPL_NMTMNU_NODE_FLAG_NOT_SCANNED;

                EplNmtMnuInstance_g.m_uiSignalSlaveCount++;
                // signal slave counter shall be decremented if timeout elapsed and regardless of an error
                // mandatory slave counter shall be decremented if timeout elapsed and no error occured
            }
        }
    }

    Ret = kEplSuccessful;

Exit:
    return Ret;
}


//---------------------------------------------------------------------------
//
// Function:    EplNmtMnuNodeCheckCom
//
// Description: checks communication of the specified node.
//              That means wait some time and if no error occured everything
//              is OK.
//
// Parameters:  uiNodeId_p              = node ID
//              pNodeInfo_p             = pointer to internal node info structure
//
// Returns:     tEplKernel              = error code
//
// State:
//
//---------------------------------------------------------------------------

static tEplKernel EplNmtMnuNodeCheckCom(unsigned int uiNodeId_p, tEplNmtMnuNodeInfo* pNodeInfo_p)
{
tEplKernel      Ret = kEplSuccessful;
DWORD           dwNodeCfg;
tEplTimerArg    TimerArg;

    dwNodeCfg = pNodeInfo_p->m_dwNodeCfg;
    if (((dwNodeCfg & EPL_NODEASSIGN_ASYNCONLY_NODE) == 0)
        && (EplNmtMnuInstance_g.m_ulTimeoutCheckCom != 0L))
    {   // CN is not async-only and timeout for CheckCom was set

        // check communication,
        // that means wait some time and if no error occured everything is OK;

        // start timer (when the timer expires the CN must be still ReadyToOp)
        EPL_NMTMNU_SET_FLAGS_TIMERARG_LONGER(
                pNodeInfo_p, uiNodeId_p, TimerArg);
//        TimerArg.m_EventSink = kEplEventSinkNmtMnu;
//        TimerArg.m_Arg.m_dwVal = EPL_NMTMNU_TIMERARG_LONGER | uiNodeId_p;
        Ret = EplTimeruModifyTimerMs(&pNodeInfo_p->m_TimerHdlLonger, EplNmtMnuInstance_g.m_ulTimeoutCheckCom, TimerArg);

        // update mandatory slave counter, because timer was started
        if (Ret == kEplSuccessful)
        {
            Ret = kEplReject;
        }
    }
    else
    {   // timer was not started
        // assume everything is OK
        pNodeInfo_p->m_NodeState = kEplNmtMnuNodeStateComChecked;
    }

//Exit:
    return Ret;
}


//---------------------------------------------------------------------------
//
// Function:    EplNmtMnuStartNodes
//
// Description: really starts all nodes which are ReadyToOp and CheckCom did not fail
//
// Parameters:  (none)
//
// Returns:     tEplKernel              = error code
//
// State:
//
//---------------------------------------------------------------------------

static tEplKernel EplNmtMnuStartNodes(void)
{
tEplKernel      Ret = kEplSuccessful;
unsigned int    uiIndex;
tEplNmtMnuNodeInfo* pNodeInfo;


    if ((EplNmtMnuInstance_g.m_wFlags & EPL_NMTMNU_FLAG_HALTED) == 0)
    {   // boot process is not halted
        // send NMT command Start Node
        EplNmtMnuInstance_g.m_uiMandatorySlaveCount = 0;
        EplNmtMnuInstance_g.m_uiSignalSlaveCount = 0;
        // reset flag that application was informed about possible state change
        EplNmtMnuInstance_g.m_wFlags &= ~EPL_NMTMNU_FLAG_APP_INFORMED;

        pNodeInfo = EplNmtMnuInstance_g.m_aNodeInfo;
        for (uiIndex = 1; uiIndex <= tabentries(EplNmtMnuInstance_g.m_aNodeInfo); uiIndex++, pNodeInfo++)
        {
            if (pNodeInfo->m_NodeState == kEplNmtMnuNodeStateComChecked)
            {
                if ((EplNmtMnuInstance_g.m_dwNmtStartup & EPL_NMTST_STARTALLNODES) == 0)
                {
//                    EPL_NMTMNU_DBG_POST_TRACE_VALUE(0,
//                                                    uiIndex,
//                                                    kEplNmtCmdStartNode);

                    Ret = EplNmtMnuSendNmtCommand(uiIndex, kEplNmtCmdStartNode);
                    if (Ret != kEplSuccessful)
                    {
                        goto Exit;
                    }
                }

                if ((pNodeInfo->m_dwNodeCfg & EPL_NODEASSIGN_MANDATORY_CN) != 0)
                {   // node is a mandatory CN
                    EplNmtMnuInstance_g.m_uiMandatorySlaveCount++;
                }

                // set flag "not scanned"
                pNodeInfo->m_wFlags |= EPL_NMTMNU_NODE_FLAG_NOT_SCANNED;

                EplNmtMnuInstance_g.m_uiSignalSlaveCount++;
                // signal slave counter shall be decremented if StatusRequest was sent once to a CN
                // mandatory slave counter shall be decremented if mandatory CN is OPERATIONAL
            }
        }

        // $$$ inform application if EPL_NMTST_NO_STARTNODE is set

        if ((EplNmtMnuInstance_g.m_dwNmtStartup & EPL_NMTST_STARTALLNODES) != 0)
        {
//            EPL_NMTMNU_DBG_POST_TRACE_VALUE(0,
//                                            EPL_C_ADR_BROADCAST,
//                                            kEplNmtCmdStartNode);

            Ret = EplNmtMnuSendNmtCommand(EPL_C_ADR_BROADCAST, kEplNmtCmdStartNode);
            if (Ret != kEplSuccessful)
            {
                goto Exit;
            }
        }
    }

Exit:
    return Ret;
}


//---------------------------------------------------------------------------
//
// Function:    EplNmtMnuProcessInternalEvent
//
// Description: processes internal node events
//
// Parameters:  uiNodeId_p              = node ID
//              NodeNmtState_p          = NMT state of CN
//              NodeEvent_p             = occured events
//
// Returns:     tEplKernel              = error code
//
//
// State:
//
//---------------------------------------------------------------------------

static tEplKernel EplNmtMnuProcessInternalEvent(
                                    unsigned int        uiNodeId_p,
                                    tEplNmtState        NodeNmtState_p,
                                    WORD                wErrorCode_p,
                                    tEplNmtMnuIntNodeEvent NodeEvent_p)
{
tEplKernel          Ret = kEplSuccessful;
tEplNmtState        NmtState;
tEplNmtMnuNodeInfo* pNodeInfo;
tEplTimerArg        TimerArg;

    pNodeInfo = EPL_NMTMNU_GET_NODEINFO(uiNodeId_p);
    NmtState = EplNmtuGetNmtState();
    if (NmtState <= kEplNmtMsNotActive)
    {   // MN is not active
        goto Exit;
    }

    switch (NodeEvent_p)
    {
        case kEplNmtMnuIntNodeEventIdentResponse:
        {
        BYTE    bNmtState;

//            EPL_NMTMNU_DBG_POST_TRACE_VALUE(NodeEvent_p,
//                                            uiNodeId_p,
//                                            pNodeInfo->m_NodeState);

            if ((pNodeInfo->m_NodeState != kEplNmtMnuNodeStateResetConf)
                && (pNodeInfo->m_NodeState != kEplNmtMnuNodeStateConfRestored))
            {
                pNodeInfo->m_NodeState = kEplNmtMnuNodeStateIdentified;
            }

            // reset flags ISOCHRON and NMT_CMD_ISSUED
            pNodeInfo->m_wFlags &= ~(EPL_NMTMNU_NODE_FLAG_ISOCHRON
                                     | EPL_NMTMNU_NODE_FLAG_NMT_CMD_ISSUED);

            if ((NmtState == kEplNmtMsPreOperational1)
                && ((pNodeInfo->m_wFlags & EPL_NMTMNU_NODE_FLAG_NOT_SCANNED) != 0))
            {
                // decrement only signal slave count
                EplNmtMnuInstance_g.m_uiSignalSlaveCount--;
                pNodeInfo->m_wFlags &= ~EPL_NMTMNU_NODE_FLAG_NOT_SCANNED;
            }

            // update object 0x1F8F NMT_MNNodeExpState_AU8 to PreOp1 (even if local state >= PreOp2)
            bNmtState = (BYTE) (kEplNmtCsPreOperational1 & 0xFF);
            Ret = EplObduWriteEntry(0x1F8F, uiNodeId_p, &bNmtState, 1);

            // check NMT state of CN
            Ret = EplNmtMnuCheckNmtState(uiNodeId_p, pNodeInfo, NodeNmtState_p, wErrorCode_p, NmtState);
            if (Ret != kEplSuccessful)
            {
                if (Ret == kEplReject)
                {
                    Ret = kEplSuccessful;
                }
                break;
            }

            // request StatusResponse immediately,
            // because we want a fast boot-up of CNs
            Ret = EplStatusuRequestStatusResponse(uiNodeId_p, EplNmtMnuCbStatusResponse);
            if (Ret != kEplSuccessful)
            {
//                EPL_NMTMNU_DBG_POST_TRACE_VALUE(NodeEvent_p,
//                                                uiNodeId_p,
//                                                Ret);

                if (Ret == kEplInvalidOperation)
                {   // the only situation when this should happen is, when
                    // StatusResponse was already requested from within
                    // the StatReq timer event.
                    // so ignore this error.
                    Ret = kEplSuccessful;
                }
                else
                {
                    break;
                }
            }

            if ((pNodeInfo->m_NodeState != kEplNmtMnuNodeStateResetConf)
                && (pNodeInfo->m_NodeState != kEplNmtMnuNodeStateConfRestored))
            {
                // inform application
                Ret = EplNmtMnuInstance_g.m_pfnCbNodeEvent(uiNodeId_p,
                                                           kEplNmtNodeEventFound,
                                                           NodeNmtState_p,
                                                           EPL_E_NO_ERROR,
                                                           (pNodeInfo->m_dwNodeCfg & EPL_NODEASSIGN_MANDATORY_CN) != 0);
                if (Ret == kEplReject)
                {   // interrupt boot process on user request
//                    EPL_NMTMNU_DBG_POST_TRACE_VALUE(NodeEvent_p,
//                                                    uiNodeId_p,
//                                                    ((pNodeInfo->m_NodeState << 8)
//                                                     | Ret));

                    Ret = kEplSuccessful;
                    break;
                }
                else if (Ret != kEplSuccessful)
                {
//                    EPL_NMTMNU_DBG_POST_TRACE_VALUE(NodeEvent_p,
//                                                    uiNodeId_p,
//                                                    ((pNodeInfo->m_NodeState << 8)
//                                                     | Ret));

                    break;
                }
            }

            // continue BootStep1
        }

        case kEplNmtMnuIntNodeEventBoot:
        {

            // $$$ check identification (vendor ID, product code, revision no, serial no)

            if (pNodeInfo->m_NodeState == kEplNmtMnuNodeStateIdentified)
            {
                // $$$ check software

                // check/start configuration
                // inform application
                Ret = EplNmtMnuInstance_g.m_pfnCbNodeEvent(uiNodeId_p,
                                                           kEplNmtNodeEventCheckConf,
                                                           NodeNmtState_p,
                                                           EPL_E_NO_ERROR,
                                                           (pNodeInfo->m_dwNodeCfg & EPL_NODEASSIGN_MANDATORY_CN) != 0);
                if (Ret == kEplReject)
                {   // interrupt boot process on user request
//                    EPL_NMTMNU_DBG_POST_TRACE_VALUE(kEplNmtMnuIntNodeEventBoot,
//                                                    uiNodeId_p,
//                                                    ((pNodeInfo->m_NodeState << 8)
//                                                     | Ret));

                    Ret = kEplSuccessful;
                    break;
                }
                else if (Ret != kEplSuccessful)
                {
//                    EPL_NMTMNU_DBG_POST_TRACE_VALUE(kEplNmtMnuIntNodeEventBoot,
//                                                    uiNodeId_p,
//                                                    ((pNodeInfo->m_NodeState << 8)
//                                                     | Ret));

                    break;
                }
            }
            else if (pNodeInfo->m_NodeState == kEplNmtMnuNodeStateConfRestored)
            {
                // check/start configuration
                // inform application
                Ret = EplNmtMnuInstance_g.m_pfnCbNodeEvent(uiNodeId_p,
                                                           kEplNmtNodeEventUpdateConf,
                                                           NodeNmtState_p,
                                                           EPL_E_NO_ERROR,
                                                           (pNodeInfo->m_dwNodeCfg & EPL_NODEASSIGN_MANDATORY_CN) != 0);
                if (Ret == kEplReject)
                {   // interrupt boot process on user request
//                    EPL_NMTMNU_DBG_POST_TRACE_VALUE(kEplNmtMnuIntNodeEventBoot,
//                                                    uiNodeId_p,
//                                                    ((pNodeInfo->m_NodeState << 8)
//                                                     | Ret));

                    Ret = kEplSuccessful;
                    break;
                }
                else if (Ret != kEplSuccessful)
                {
//                    EPL_NMTMNU_DBG_POST_TRACE_VALUE(kEplNmtMnuIntNodeEventBoot,
//                                                    uiNodeId_p,
//                                                    ((pNodeInfo->m_NodeState << 8)
//                                                     | Ret));

                    break;
                }
            }
            else if (pNodeInfo->m_NodeState != kEplNmtMnuNodeStateResetConf)
            {   // wrong CN state
                // ignore event
                break;
            }

            // we assume configuration is OK

            // continue BootStep1
        }

        case kEplNmtMnuIntNodeEventConfigured:
        {
            if ((pNodeInfo->m_NodeState != kEplNmtMnuNodeStateIdentified)
                && (pNodeInfo->m_NodeState != kEplNmtMnuNodeStateConfRestored)
                && (pNodeInfo->m_NodeState != kEplNmtMnuNodeStateResetConf))
            {   // wrong CN state
                // ignore event
                break;
            }

            pNodeInfo->m_NodeState = kEplNmtMnuNodeStateConfigured;

            if (NmtState == kEplNmtMsPreOperational1)
            {
                if ((pNodeInfo->m_dwNodeCfg & EPL_NODEASSIGN_MANDATORY_CN) != 0)
                {   // decrement mandatory CN counter
                    EplNmtMnuInstance_g.m_uiMandatorySlaveCount--;
                }
            }
            else
            {
                // put optional node to next step (BootStep2)
                Ret = EplNmtMnuNodeBootStep2(uiNodeId_p, pNodeInfo);
            }
            break;
        }

        case kEplNmtMnuIntNodeEventNoIdentResponse:
        {
            if ((NmtState == kEplNmtMsPreOperational1)
                && ((pNodeInfo->m_wFlags & EPL_NMTMNU_NODE_FLAG_NOT_SCANNED) != 0))
            {
                // decrement only signal slave count
                EplNmtMnuInstance_g.m_uiSignalSlaveCount--;
                pNodeInfo->m_wFlags &= ~EPL_NMTMNU_NODE_FLAG_NOT_SCANNED;
            }

            if ((pNodeInfo->m_NodeState != kEplNmtMnuNodeStateResetConf)
                && (pNodeInfo->m_NodeState != kEplNmtMnuNodeStateConfRestored))
            {
                pNodeInfo->m_NodeState = kEplNmtMnuNodeStateUnknown;
            }

            // $$$ d.k. check start time for 0x1F89/2 MNTimeoutPreOp1_U32
            // $$$ d.k. check individual timeout 0x1F89/6 MNIdentificationTimeout_U32
            // if mandatory node and timeout elapsed -> halt boot procedure
            // trigger IdentRequest again (if >= PreOp2, after delay)
            if (NmtState >= kEplNmtMsPreOperational2)
            {   // start timer
                EPL_NMTMNU_SET_FLAGS_TIMERARG_IDENTREQ(
                        pNodeInfo, uiNodeId_p, TimerArg);
//                TimerArg.m_EventSink = kEplEventSinkNmtMnu;
//                TimerArg.m_Arg.m_dwVal = EPL_NMTMNU_TIMERARG_IDENTREQ | uiNodeId_p;
/*
                EPL_NMTMNU_DBG_POST_TRACE_VALUE(kEplNmtMnuIntNodeEventNoIdentResponse,
                                                uiNodeId_p,
                                                ((pNodeInfo->m_NodeState << 8)
                                                 | 0x80
                                                 | ((pNodeInfo->m_wFlags & EPL_NMTMNU_NODE_FLAG_COUNT_STATREQ) >> 6)
                                                 | ((TimerArg.m_Arg.m_dwVal & EPL_NMTMNU_TIMERARG_COUNT_SR) >> 8)));
*/
                Ret = EplTimeruModifyTimerMs(&pNodeInfo->m_TimerHdlStatReq, EplNmtMnuInstance_g.m_ulStatusRequestDelay, TimerArg);
            }
            else
            {   // trigger IdentRequest immediately
                Ret = EplIdentuRequestIdentResponse(uiNodeId_p, EplNmtMnuCbIdentResponse);
            }
            break;
        }

        case kEplNmtMnuIntNodeEventStatusResponse:
        {
            if ((NmtState >= kEplNmtMsPreOperational2)
                && ((pNodeInfo->m_wFlags & EPL_NMTMNU_NODE_FLAG_NOT_SCANNED) != 0))
            {
                // decrement only signal slave count if checked once for ReadyToOp, CheckCom, Operational
                EplNmtMnuInstance_g.m_uiSignalSlaveCount--;
                pNodeInfo->m_wFlags &= ~EPL_NMTMNU_NODE_FLAG_NOT_SCANNED;
            }

            // check NMT state of CN
            Ret = EplNmtMnuCheckNmtState(uiNodeId_p, pNodeInfo, NodeNmtState_p, wErrorCode_p, NmtState);
            if (Ret != kEplSuccessful)
            {
                if (Ret == kEplReject)
                {
                    Ret = kEplSuccessful;
                }
                break;
            }

            if (NmtState == kEplNmtMsPreOperational1)
            {
                // request next StatusResponse immediately
                Ret = EplStatusuRequestStatusResponse(uiNodeId_p, EplNmtMnuCbStatusResponse);
                if (Ret != kEplSuccessful)
                {
//                    EPL_NMTMNU_DBG_POST_TRACE_VALUE(NodeEvent_p,
//                                                    uiNodeId_p,
//                                                    Ret);
                }

            }
            else if ((pNodeInfo->m_wFlags & EPL_NMTMNU_NODE_FLAG_ISOCHRON) == 0)
            {   // start timer
                // not isochronously accessed CN (e.g. async-only or stopped CN)
                EPL_NMTMNU_SET_FLAGS_TIMERARG_STATREQ(
                        pNodeInfo, uiNodeId_p, TimerArg);
//                TimerArg.m_EventSink = kEplEventSinkNmtMnu;
//                TimerArg.m_Arg.m_dwVal = EPL_NMTMNU_TIMERARG_STATREQ | uiNodeId_p;
/*
                EPL_NMTMNU_DBG_POST_TRACE_VALUE(kEplNmtMnuIntNodeEventStatusResponse,
                                                uiNodeId_p,
                                                ((pNodeInfo->m_NodeState << 8)
                                                 | 0x80
                                                 | ((pNodeInfo->m_wFlags & EPL_NMTMNU_NODE_FLAG_COUNT_STATREQ) >> 6)
                                                 | ((TimerArg.m_Arg.m_dwVal & EPL_NMTMNU_TIMERARG_COUNT_SR) >> 8)));
*/
                Ret = EplTimeruModifyTimerMs(&pNodeInfo->m_TimerHdlStatReq, EplNmtMnuInstance_g.m_ulStatusRequestDelay, TimerArg);
            }

            break;
        }

        case kEplNmtMnuIntNodeEventNoStatusResponse:
        {
            // function CheckNmtState sets node state to unknown if necessary
/*
            if ((NmtState >= kEplNmtMsPreOperational2)
                && ((pNodeInfo->m_wFlags & EPL_NMTMNU_NODE_FLAG_NOT_SCANNED) != 0))
            {
                // decrement only signal slave count if checked once for ReadyToOp, CheckCom, Operational
                EplNmtMnuInstance_g.m_uiSignalSlaveCount--;
                pNodeInfo->m_wFlags &= ~EPL_NMTMNU_NODE_FLAG_NOT_SCANNED;
            }
*/
            // check NMT state of CN
            Ret = EplNmtMnuCheckNmtState(uiNodeId_p, pNodeInfo, NodeNmtState_p, wErrorCode_p, NmtState);
            if (Ret != kEplSuccessful)
            {
                if (Ret == kEplReject)
                {
                    Ret = kEplSuccessful;
                }
                break;
            }

            break;
        }

        case kEplNmtMnuIntNodeEventError:
        {   // currently only issued on kEplNmtNodeCommandConfErr

            if ((pNodeInfo->m_NodeState != kEplNmtMnuNodeStateIdentified)
                && (pNodeInfo->m_NodeState != kEplNmtMnuNodeStateConfRestored))
            {   // wrong CN state
                // ignore event
                break;
            }

            // check NMT state of CN
            Ret = EplNmtMnuCheckNmtState(uiNodeId_p, pNodeInfo, kEplNmtCsNotActive, wErrorCode_p, NmtState);
            if (Ret != kEplSuccessful)
            {
                if (Ret == kEplReject)
                {
                    Ret = kEplSuccessful;
                }
                break;
            }

            break;
        }

        case kEplNmtMnuIntNodeEventExecResetNode:
        {
            if (pNodeInfo->m_NodeState != kEplNmtMnuNodeStateIdentified)
            {   // wrong CN state
                // ignore event
                break;
            }

            pNodeInfo->m_NodeState = kEplNmtMnuNodeStateConfRestored;

//            EPL_NMTMNU_DBG_POST_TRACE_VALUE(NodeEvent_p,
//                                            uiNodeId_p,
//                                            (((NodeNmtState_p & 0xFF) << 8)
//                                            | kEplNmtCmdResetNode));

            // send NMT reset node to CN for activation of restored configuration
            Ret = EplNmtMnuSendNmtCommand(uiNodeId_p, kEplNmtCmdResetNode);

            break;
        }

        case kEplNmtMnuIntNodeEventExecResetConf:
        {
            if ((pNodeInfo->m_NodeState != kEplNmtMnuNodeStateIdentified)
                && (pNodeInfo->m_NodeState != kEplNmtMnuNodeStateConfRestored))
            {   // wrong CN state
                // ignore event
                break;
            }

            pNodeInfo->m_NodeState = kEplNmtMnuNodeStateResetConf;

//            EPL_NMTMNU_DBG_POST_TRACE_VALUE(NodeEvent_p,
//                                            uiNodeId_p,
//                                            (((NodeNmtState_p & 0xFF) << 8)
//                                            | kEplNmtCmdResetConfiguration));

            // send NMT reset configuration to CN for activation of configuration
            Ret = EplNmtMnuSendNmtCommand(uiNodeId_p, kEplNmtCmdResetConfiguration);

            break;
        }

        case kEplNmtMnuIntNodeEventHeartbeat:
        {
/*
            if ((NmtState >= kEplNmtMsPreOperational2)
                && ((pNodeInfo->m_wFlags & EPL_NMTMNU_NODE_FLAG_NOT_SCANNED) != 0))
            {
                // decrement only signal slave count if checked once for ReadyToOp, CheckCom, Operational
                EplNmtMnuInstance_g.m_uiSignalSlaveCount--;
                pNodeInfo->m_wFlags &= ~EPL_NMTMNU_NODE_FLAG_NOT_SCANNED;
            }
*/
            // check NMT state of CN
            Ret = EplNmtMnuCheckNmtState(uiNodeId_p, pNodeInfo, NodeNmtState_p, wErrorCode_p, NmtState);
            if (Ret != kEplSuccessful)
            {
                if (Ret == kEplReject)
                {
                    Ret = kEplSuccessful;
                }
                break;
            }

            break;
        }

        case kEplNmtMnuIntNodeEventTimerIdentReq:
        {
           // EPL_DBGLVL_NMTMN_TRACE1("TimerStatReq->IdentReq(%02X)\n", uiNodeId_p);
            // trigger IdentRequest again
            Ret = EplIdentuRequestIdentResponse(uiNodeId_p, EplNmtMnuCbIdentResponse);
            if (Ret != kEplSuccessful)
            {
//                EPL_NMTMNU_DBG_POST_TRACE_VALUE(NodeEvent_p,
//                                                uiNodeId_p,
//                                                (((NodeNmtState_p & 0xFF) << 8)
//                                                 | Ret));
                if (Ret == kEplInvalidOperation)
                {   // this can happen because of a bug in EplTimeruLinuxKernel.c
                    // so ignore this error.
                    Ret = kEplSuccessful;
                }
            }

            break;
        }

        case kEplNmtMnuIntNodeEventTimerStateMon:
        {
            // reset NMT state change flag
            // because from now on the CN must have the correct NMT state
            pNodeInfo->m_wFlags &= ~EPL_NMTMNU_NODE_FLAG_NMT_CMD_ISSUED;

            // continue with normal StatReq processing
        }

        case kEplNmtMnuIntNodeEventTimerStatReq:
        {
            //EPL_DBGLVL_NMTMN_TRACE1("TimerStatReq->StatReq(%02X)\n", uiNodeId_p);
            // request next StatusResponse
            Ret = EplStatusuRequestStatusResponse(uiNodeId_p, EplNmtMnuCbStatusResponse);
            if (Ret != kEplSuccessful)
            {
//                EPL_NMTMNU_DBG_POST_TRACE_VALUE(NodeEvent_p,
//                                                uiNodeId_p,
//                                                (((NodeNmtState_p & 0xFF) << 8)
//                                                 | Ret));
                if (Ret == kEplInvalidOperation)
                {   // the only situation when this should happen is, when
                    // StatusResponse was already requested while processing
                    // event IdentResponse.
                    // so ignore this error.
                    Ret = kEplSuccessful;
                }
            }

            break;
        }

        case kEplNmtMnuIntNodeEventTimerLonger:
        {
            switch (pNodeInfo->m_NodeState)
            {
                case kEplNmtMnuNodeStateConfigured:
                {   // node should be ReadyToOp but it is not

                    // check NMT state which shall be intentionally wrong, so that ERROR_TREATMENT will be started
                    Ret = EplNmtMnuCheckNmtState(uiNodeId_p, pNodeInfo, kEplNmtCsNotActive, EPL_E_NMT_BPO2, NmtState);
                    if (Ret != kEplSuccessful)
                    {
                        if (Ret == kEplReject)
                        {
                            Ret = kEplSuccessful;
                        }
                        break;
                    }

                    break;
                }

                case kEplNmtMnuNodeStateReadyToOp:
                {   // CheckCom finished successfully

                    pNodeInfo->m_NodeState = kEplNmtMnuNodeStateComChecked;

                    if ((pNodeInfo->m_wFlags & EPL_NMTMNU_NODE_FLAG_NOT_SCANNED) != 0)
                    {
                        // decrement only signal slave count if checked once for ReadyToOp, CheckCom, Operational
                        EplNmtMnuInstance_g.m_uiSignalSlaveCount--;
                        pNodeInfo->m_wFlags &= ~EPL_NMTMNU_NODE_FLAG_NOT_SCANNED;
                    }

                    if ((pNodeInfo->m_dwNodeCfg & EPL_NODEASSIGN_MANDATORY_CN) != 0)
                    {
                        // decrement mandatory slave counter
                        EplNmtMnuInstance_g.m_uiMandatorySlaveCount--;
                    }
                    if (NmtState != kEplNmtMsReadyToOperate)
                    {
//                        EPL_NMTMNU_DBG_POST_TRACE_VALUE(NodeEvent_p,
//                                                        uiNodeId_p,
//                                                        (((NodeNmtState_p & 0xFF) << 8)
//                                                        | kEplNmtCmdStartNode));

                        // start optional CN
                        Ret = EplNmtMnuSendNmtCommand(uiNodeId_p, kEplNmtCmdStartNode);
                    }
                    break;
                }

                default:
                {
                    break;
                }
            }
            break;
        }

        case kEplNmtMnuIntNodeEventNmtCmdSent:
        {
        BYTE    bNmtState;

            // update expected NMT state with the one that results
            // from the sent NMT command
            bNmtState = (BYTE) (NodeNmtState_p & 0xFF);

            // write object 0x1F8F NMT_MNNodeExpState_AU8
            Ret = EplObduWriteEntry(0x1F8F, uiNodeId_p, &bNmtState, 1);
            if (Ret != kEplSuccessful)
            {
                goto Exit;
            }

            if (NodeNmtState_p == kEplNmtCsNotActive)
            {   // restart processing with IdentRequest
                EPL_NMTMNU_SET_FLAGS_TIMERARG_IDENTREQ(
                        pNodeInfo, uiNodeId_p, TimerArg);
            }
            else
            {   // monitor NMT state change with StatusRequest after
                // the corresponding delay;
                // until then wrong NMT states will be ignored
                EPL_NMTMNU_SET_FLAGS_TIMERARG_STATE_MON(
                        pNodeInfo, uiNodeId_p, TimerArg);

                // set NMT state change flag
                pNodeInfo->m_wFlags |= EPL_NMTMNU_NODE_FLAG_NMT_CMD_ISSUED;
            }

            Ret = EplTimeruModifyTimerMs(&pNodeInfo->m_TimerHdlStatReq, EplNmtMnuInstance_g.m_ulStatusRequestDelay, TimerArg);

            // finish processing, because NmtState_p is the expected and not the current state
            goto Exit;
        }

        default:
        {
            break;
        }
    }

    // check if network is ready to change local NMT state and this was not done before
    if ((EplNmtMnuInstance_g.m_wFlags & (EPL_NMTMNU_FLAG_HALTED | EPL_NMTMNU_FLAG_APP_INFORMED)) == 0)
    {   // boot process is not halted
        switch (NmtState)
        {
            case kEplNmtMsPreOperational1:
            {
                if ((EplNmtMnuInstance_g.m_uiSignalSlaveCount == 0)
                    && (EplNmtMnuInstance_g.m_uiMandatorySlaveCount == 0))
                {   // all optional CNs scanned once and all mandatory CNs configured successfully
                    EplNmtMnuInstance_g.m_wFlags |= EPL_NMTMNU_FLAG_APP_INFORMED;
                    // inform application
                    Ret = EplNmtMnuInstance_g.m_pfnCbBootEvent(kEplNmtBootEventBootStep1Finish,
                                                               NmtState,
                                                               EPL_E_NO_ERROR);
                    if (Ret != kEplSuccessful)
                    {
                        if (Ret == kEplReject)
                        {
                            // wait for application
                            Ret = kEplSuccessful;
                        }
                        break;
                    }
                    // enter PreOp2
                    Ret = EplNmtuNmtEvent(kEplNmtEventAllMandatoryCNIdent);
                }
                break;
            }

            case kEplNmtMsPreOperational2:
            {
                if ((EplNmtMnuInstance_g.m_uiSignalSlaveCount == 0)
                    && (EplNmtMnuInstance_g.m_uiMandatorySlaveCount == 0))
                {   // all optional CNs checked once for ReadyToOp and all mandatory CNs are ReadyToOp
                    EplNmtMnuInstance_g.m_wFlags |= EPL_NMTMNU_FLAG_APP_INFORMED;
                    // inform application
                    Ret = EplNmtMnuInstance_g.m_pfnCbBootEvent(kEplNmtBootEventBootStep2Finish,
                                                               NmtState,
                                                               EPL_E_NO_ERROR);
                    if (Ret != kEplSuccessful)
                    {
                        if (Ret == kEplReject)
                        {
                            // wait for application
                            Ret = kEplSuccessful;
                        }
                        break;
                    }
                    // enter ReadyToOp
                    Ret = EplNmtuNmtEvent(kEplNmtEventEnterReadyToOperate);
                }
                break;
            }

            case kEplNmtMsReadyToOperate:
            {
                if ((EplNmtMnuInstance_g.m_uiSignalSlaveCount == 0)
                    && (EplNmtMnuInstance_g.m_uiMandatorySlaveCount == 0))
                {   // all CNs checked for errorless communication
                    EplNmtMnuInstance_g.m_wFlags |= EPL_NMTMNU_FLAG_APP_INFORMED;
                    // inform application
                    Ret = EplNmtMnuInstance_g.m_pfnCbBootEvent(kEplNmtBootEventCheckComFinish,
                                                               NmtState,
                                                               EPL_E_NO_ERROR);
                    if (Ret != kEplSuccessful)
                    {
                        if (Ret == kEplReject)
                        {
                            // wait for application
                            Ret = kEplSuccessful;
                        }
                        break;
                    }
                    // enter Operational
                    Ret = EplNmtuNmtEvent(kEplNmtEventEnterMsOperational);
                }
                break;
            }

            case kEplNmtMsOperational:
            {
                if ((EplNmtMnuInstance_g.m_uiSignalSlaveCount == 0)
                    && (EplNmtMnuInstance_g.m_uiMandatorySlaveCount == 0))
                {   // all optional CNs scanned once and all mandatory CNs are OPERATIONAL
                    EplNmtMnuInstance_g.m_wFlags |= EPL_NMTMNU_FLAG_APP_INFORMED;
                    // inform application
                    Ret = EplNmtMnuInstance_g.m_pfnCbBootEvent(kEplNmtBootEventOperational,
                                                               NmtState,
                                                               EPL_E_NO_ERROR);
                    if (Ret != kEplSuccessful)
                    {
                        if (Ret == kEplReject)
                        {
                            // ignore error code
                            Ret = kEplSuccessful;
                        }
                        break;
                    }
                }
                break;
            }

            default:
            {
                break;
            }
        }
    }

Exit:
    return Ret;
}


//---------------------------------------------------------------------------
//
// Function:    EplNmtMnuCheckNmtState
//
// Description: checks the NMT state, i.e. evaluates it with object 0x1F8F
//              NMT_MNNodeExpState_AU8 and updates object 0x1F8E
//              NMT_MNNodeCurrState_AU8.
//              It manipulates m_NodeState in internal node info structure.
//
// Parameters:  uiNodeId_p              = node ID
//              NodeNmtState_p          = NMT state of CN
//
// Returns:     tEplKernel              = error code
//                  kEplReject          = CN was in wrong state and has been reset
//
// State:
//
//---------------------------------------------------------------------------

static tEplKernel EplNmtMnuCheckNmtState(
                                    unsigned int        uiNodeId_p,
                                    tEplNmtMnuNodeInfo* pNodeInfo_p,
                                    tEplNmtState        NodeNmtState_p,
                                    WORD                wErrorCode_p,
                                    tEplNmtState        LocalNmtState_p)
{
tEplKernel      Ret = kEplSuccessful;
tEplObdSize     ObdSize;
BYTE            bNmtState;
BYTE            bNmtStatePrev;
tEplNmtState    ExpNmtState;

    ObdSize = 1;
    // read object 0x1F8F NMT_MNNodeExpState_AU8
    Ret = EplObduReadEntry(0x1F8F, uiNodeId_p, &bNmtState, &ObdSize);
    if (Ret != kEplSuccessful)
    {
        goto Exit;
    }

    // compute expected NMT state
    ExpNmtState = (tEplNmtState) (bNmtState | EPL_NMT_TYPE_CS);
    // compute BYTE of current NMT state
    bNmtState = ((BYTE) NodeNmtState_p & 0xFF);

    if (ExpNmtState == kEplNmtCsNotActive)
    {   // ignore the current state, because the CN shall be not active
        Ret = kEplReject;
        goto Exit;
    }
    else if ((ExpNmtState == kEplNmtCsPreOperational2)
         && (NodeNmtState_p == kEplNmtCsReadyToOperate))
    {   // CN switched to ReadyToOp
        // delete timer for timeout handling
        Ret = EplTimeruDeleteTimer(&pNodeInfo_p->m_TimerHdlLonger);
        if (Ret != kEplSuccessful)
        {
            goto Exit;
        }
        pNodeInfo_p->m_NodeState = kEplNmtMnuNodeStateReadyToOp;

        // update object 0x1F8F NMT_MNNodeExpState_AU8 to ReadyToOp
        Ret = EplObduWriteEntry(0x1F8F, uiNodeId_p, &bNmtState, 1);
        if (Ret != kEplSuccessful)
        {
            goto Exit;
        }

        if ((pNodeInfo_p->m_dwNodeCfg & EPL_NODEASSIGN_MANDATORY_CN) != 0)
        {   // node is a mandatory CN -> decrement counter
            EplNmtMnuInstance_g.m_uiMandatorySlaveCount--;
        }
        if (LocalNmtState_p >= kEplNmtMsReadyToOperate)
        {   // start procedure CheckCommunication for this node
            Ret = EplNmtMnuNodeCheckCom(uiNodeId_p, pNodeInfo_p);
            if (Ret != kEplSuccessful)
            {
                goto Exit;
            }

            if ((LocalNmtState_p == kEplNmtMsOperational)
                && (pNodeInfo_p->m_NodeState == kEplNmtMnuNodeStateComChecked))
            {
//                EPL_NMTMNU_DBG_POST_TRACE_VALUE(0,
//                                                uiNodeId_p,
//                                                (((NodeNmtState_p & 0xFF) << 8)
//                                                | kEplNmtCmdStartNode));

                // immediately start optional CN, because communication is always OK (e.g. async-only CN)
                Ret = EplNmtMnuSendNmtCommand(uiNodeId_p, kEplNmtCmdStartNode);
                if (Ret != kEplSuccessful)
                {
                    goto Exit;
                }
            }
        }

    }
    else if ((ExpNmtState == kEplNmtCsReadyToOperate)
             && (NodeNmtState_p == kEplNmtCsOperational))
    {   // CN switched to OPERATIONAL
        pNodeInfo_p->m_NodeState = kEplNmtMnuNodeStateOperational;

        if ((pNodeInfo_p->m_dwNodeCfg & EPL_NODEASSIGN_MANDATORY_CN) != 0)
        {   // node is a mandatory CN -> decrement counter
            EplNmtMnuInstance_g.m_uiMandatorySlaveCount--;
        }

    }
    else if ((ExpNmtState != NodeNmtState_p)
             && !((ExpNmtState == kEplNmtCsPreOperational1)
                  && (NodeNmtState_p == kEplNmtCsPreOperational2)))
    {   // CN is not in expected NMT state (without the exceptions above)
    WORD wbeErrorCode;

        if ((pNodeInfo_p->m_wFlags & EPL_NMTMNU_NODE_FLAG_NOT_SCANNED) != 0)
        {
            // decrement only signal slave count if checked once
            EplNmtMnuInstance_g.m_uiSignalSlaveCount--;
            pNodeInfo_p->m_wFlags &= ~EPL_NMTMNU_NODE_FLAG_NOT_SCANNED;
        }

        if (pNodeInfo_p->m_NodeState == kEplNmtMnuNodeStateUnknown)
        {   // CN is already in state unknown, which means that it got
            // NMT reset command earlier
            goto Exit;
        }

        // -> CN is in wrong NMT state
        pNodeInfo_p->m_NodeState = kEplNmtMnuNodeStateUnknown;

        if (wErrorCode_p == 0)
        {   // assume wrong NMT state error
            if ((pNodeInfo_p->m_wFlags & EPL_NMTMNU_NODE_FLAG_NMT_CMD_ISSUED) != 0)
            {   // NMT command has been just issued;
                // ignore wrong NMT state until timer expires;
                // other errors like LOSS_PRES_TH are still processed
                goto Exit;
            }

            wErrorCode_p = EPL_E_NMT_WRONG_STATE;
        }

        BENCHMARK_MOD_07_TOGGLE(7);

        // $$$ start ERROR_TREATMENT and inform application
        Ret = EplNmtMnuInstance_g.m_pfnCbNodeEvent(uiNodeId_p,
                                                   kEplNmtNodeEventError,
                                                   NodeNmtState_p,
                                                   wErrorCode_p,
                                                   (pNodeInfo_p->m_dwNodeCfg & EPL_NODEASSIGN_MANDATORY_CN) != 0);
        if (Ret != kEplSuccessful)
        {
            goto Exit;
        }

//        EPL_NMTMNU_DBG_POST_TRACE_VALUE(0,
//                                        uiNodeId_p,
//                                        (((NodeNmtState_p & 0xFF) << 8)
//                                        | kEplNmtCmdResetNode));

        // reset CN
        // store error code in NMT command data for diagnostic purpose
        AmiSetWordToLe(&wbeErrorCode, wErrorCode_p);
        Ret = EplNmtMnuSendNmtCommandEx(uiNodeId_p, kEplNmtCmdResetNode, &wbeErrorCode, sizeof (wbeErrorCode));
        if (Ret == kEplSuccessful)
        {
            Ret = kEplReject;
        }

        // d.k. continue with updating the current NMT state of the CN
//        goto Exit;
    }

    // check if NMT_MNNodeCurrState_AU8 has to be changed
    ObdSize = 1;
    Ret = EplObduReadEntry(0x1F8E, uiNodeId_p, &bNmtStatePrev, &ObdSize);
    if (Ret != kEplSuccessful)
    {
        goto Exit;
    }
    if (bNmtState != bNmtStatePrev)
    {
        // update object 0x1F8E NMT_MNNodeCurrState_AU8
        Ret = EplObduWriteEntry(0x1F8E, uiNodeId_p, &bNmtState, 1);
        if (Ret != kEplSuccessful)
        {
            goto Exit;
        }
        Ret = EplNmtMnuInstance_g.m_pfnCbNodeEvent(uiNodeId_p,
                                               kEplNmtNodeEventNmtState,
                                               NodeNmtState_p,
                                               wErrorCode_p,
                                               (pNodeInfo_p->m_dwNodeCfg & EPL_NODEASSIGN_MANDATORY_CN) != 0);
        if (Ret != kEplSuccessful)
        {
            goto Exit;
        }
    }

Exit:
    return Ret;
}

//---------------------------------------------------------------------------
//
// Function:    EplNmtMnuReset
//
// Description: reset internal structures, e.g. timers
//
// Parameters:  void
//
// Returns:     tEplKernel              = error code
//
// State:
//
//---------------------------------------------------------------------------

static tEplKernel EplNmtMnuReset(void)
{
tEplKernel  Ret;
int         iIndex;

    Ret = EplTimeruDeleteTimer(&EplNmtMnuInstance_g.m_TimerHdlNmtState);

    for (iIndex = 1; iIndex <= tabentries (EplNmtMnuInstance_g.m_aNodeInfo); iIndex++)
    {
        // delete timer handles
        Ret = EplTimeruDeleteTimer(&EPL_NMTMNU_GET_NODEINFO(iIndex)->m_TimerHdlStatReq);
        Ret = EplTimeruDeleteTimer(&EPL_NMTMNU_GET_NODEINFO(iIndex)->m_TimerHdlLonger);
    }

    return Ret;
}


#endif // #if(((EPL_MODULE_INTEGRATION) & (EPL_MODULE_NMT_MN)) != 0)

// EOF
