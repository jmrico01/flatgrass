#include <sysutil_ident.h>
SYSUTIL_IDENT_RCSID(momsrvr_processor_cpp, "$Id$ $CSID$")
#include <cmath>
#include <iostream>
#include <string>

#include <a_baslt_basclient.h>
#include <afmt_fmem_bit_api_bank1_chk.h>
#include <afmt_fmem_bit_api_bank2_chk.h>
#include <aim_reporting_util.h>
#include <aimcommon_outparam.h>
#include <aimcommon_pricingnumber.h>
#include <aimcommon_ticketnumber.h>
#include <basapi_tcpclient.h>
#include <bdet_datetime.h>
#include <bdlt_datetz.h>
#include <bsct_serviceinformation.h>
#include <bsct_useridentification.h>
#include <bsls_assert.h>
#include <bteso_resolveutil.h>
#include <cfrmf.h>
#include <flowbotclient_messages.h>
#include <flowbotclient_returncode.h>
#include <gutz_metrics.h>
#include <momglobaldefs.h>
#include <momsrvr_flowreturncode.h>
#include <momsrvr_processor.h>
#include <momssisvmsg_api.h>
#include <momtk_defs.h>
#include <products.h>
#include <ssisvcmsg_api.h>
#include <trak_api.h>
#include <ts_ftext_ftswap_cache.h>
#include <tsgdblong.h>

#include <bbit/201401/bbit_send_all_trades_to_emir_trigger.h> // BREG 182508
#include <bbit/201404/bbit_mom_flow_remove_temp_allocated_status.h> // BREG 191350
#include <bbit/201608/bbit_momsrvr_using_flowbot.h> // BREG 264354
#include <bbit/201804/bbit_enable_ctm_autorelease_in_tc_flow.h> // BREG 317032
#include <bbit/201807/bbit_use_actionsv_for_momsrvr.h> // BREG 326133
#include <bbit/201902/bbit_add_tkt_during_xpt_alloc_upd.h> // BREG 345671
#include <bbit/201902/bbit_process_rabbitmq_tradeworkfloweventsconsumer.h> // BREG 345667
#include <bbit/201906/bbit_momsrvr_tradefeed_in_sendtktout.h> // BREG 356801
#include <bbit/201908/bbit_skip_released_step_for_xpt_alloc.h> // BREG 360874

extern "C" {

#include <accftrn_readwrite.h>
#include <audit_tkt_audt.h>
#include <audit_tkt_event.h>
#include <audit_tkt_id.h>
#include <audt_interface.h>
#include <bitlib.h>
#include <calcrt2.h>
#include <calcrt2_wrap_w.h>
#include <comdb2_sqlexception.h>
#include <comdb2_sqlhandle.h>
#include <corpsflg.h>
#include <cr_clear_w.h>
#include <cr_firm.h>
#include <cr_offline_fmt_w.h>
#include <cr_ok_rcode.h>
#include <cr_trader.h>
#include <curlib_fortran_funcs.h>
#include <frectyp.h>
#include <ftrndb.h>
#include <fx_iso2lbl.h>
#include <getsec7_w.h>
#include <interbig.h>
#include <logger.h>
#include <momtk_utils.h>
#include <newtarl_util.h>
#include <parmcm.h>
#include <parmcm3.h>
#include <parmcm4.h>
#include <parmcm6.h>
#include <poms_util.h>
#include <scus_fstsndreq.h>
#include <scus_util.h>
#include <sendtkt2_fortran_funcs.h>
#include <swapSecurityTypes.h>
#include <swap_tkt_util.h>
#include <trim.h>
#include <ts_real8_comparison.h>
#include <tsamSys.h>
#include <tsam_trdutil.h>
#include <tsam_utils.h>
#include <tsctm_tctmreq.h>
#include <tsf_update_feed_status.h>
#include <uuidtocust.h>
#include <z_hftkt_hfpos_utils.h>

int isCurrentActiveVconTrade(int prcnum, int tktnum);
void ok_release_w_args(bbint32_t* prcnum, bbint32_t* tktnum, short* ok, short bypass_tasu,
                       short validate_only);
bbint32_t tctm_release_tickets(bbint32_t prcnum, bbint32_t tktnum, bbint32_t uuid, char* tsk);
void tarl_format_fundname(bbint32_t prcnum, char* outfund, int length);
void tarl_format_broker(char* outbroker, int length);
bbint32_t tctm_accftrn_idx7(bbint32_t prcnum, bbint32_t tktnum);
int auto_release_block_alloc(bbint32_t prcnum, bbint32_t tktnum, bbint32_t loadFtrn,
                             int check_tsam_autorls_setting, tsam_release_type_t rls,
                             short facility);
bbint32_t audit_tkt_valid_faudt_id_(char f_audit_id[AUDT_ID_LEN]);
int tarlrectypok_(short* rectyp);
bbint32_t uuidtouser(bbint32_t* iouuid, bbint32_t* iouser, bbint32_t* firmno);
int is_cds_();
int is_swapsdb_security_();
int scus_bulk_fstsnd(SCUS_BULK_REQUEST* req);
bbint32_t update_mom_release_bit();
int is_trade_dest_for_vcon(short facility);
short is_tsam_auto_support_yk(short ftdept);
void resftrn2_(bbint32_t*);
void savftrn2_(bbint32_t*);
}

namespace {
const string GUTS_NAMESPACE_ROOT = "momsrvr";
const string GUTS_NAMESPACE_MOMSRVR = "business";
const string GUTS_METRIC_MOMSRVR_FUNCTION = GUTS_NAMESPACE_MOMSRVR + ".function";
const string GUTS_METRIC_MOMSRVR_DBQUERY = GUTS_NAMESPACE_MOMSRVR + ".dbquery";
const string GUTS_METRIC_MOMSRVR_FLOWBOT = GUTS_NAMESPACE_MOMSRVR + ".flowbot";
const string GUTS_TAG_FUNCTION_NAME = "functionName";
const string GUTS_TAG_EVENT_TYPE = "eventType";
const string GUTS_TAG_PRICING_NUMBER = "pricingNumber";
} // namespace

namespace BloombergLP {
namespace momsrvr {

const bool IsFlowStart::True = true;
const bool IsFlowStart::False = false;

const bool IsFlowEnd::True = true;
const bool IsFlowEnd::False = false;

momsrvr_business::momsrvr_business(bslma::ManagedPtr<IUtils> utils,
                                   bslma::ManagedPtr<flowbotclient::IClient> flowbotClient)
    : d_ticketInterface(NULL)
    , d_flowbotClient(flowbotClient)
    , d_utils(utils)
{
    if (!d_utils) {
        bsl::string text(__func__);
        text += ":" + bsl::to_string(__LINE__) + "The utils are null";

        throw bsl::invalid_argument(text);
    }

    actionNumberToString[ADD_TKT] = "add_ticket";
    actionNumberToString[UPD_TKT] = "update_ticket";
    actionNumberToString[XPT_ALLOC_UPD] = "process_allocation_update";
    actionNumberToString[EMIR_RPT] = "process_reporting";
}

momsrvr_business::~momsrvr_business()
{
    // Do nothing
}

int momsrvr_business::prepareMomProcess(momtkt& ticket)
{
    using namespace aimcommon;

    BAEL_LOG_SET_CATEGORY("M_MOMSRVR.MOMSRVR_BUSINESS.PREPAREMOMPROCESS");

    // Init flow state only for master tickets
    // other tkt,, don't bother

    // load globals
    if (ticket.prcnum > 0 && ticket.tktnum > 0 && is_aim_prcnum(ticket.prcnum)) {
        P3PRCNUM = ticket.prcnum;

        if (!getUtils().loadMomTicket(ticket)) {
            BAEL_LOG_ERROR << "Error loading ticket"
                           << " tktnum=" << ticket.tktnum << " prcnum=" << ticket.prcnum
                           << " error_type=ticket_load_error" << BAEL_LOG_END;
            return MOMSRVR_INVALID_TICKET;
        }
    } else {
        BAEL_LOG_ERROR << "Ticket has an invalid pricing or ticket number"
                       << " tktnum=" << ticket.tktnum << " prcnum=" << ticket.prcnum
                       << " received data: " << ticket.toMessageBuf(ticket)
                       << " error_type=invalid_pricing" << BAEL_LOG_END;
        return MOMSRVR_INVALID_TICKET;
    }

    getUtils().loadParmSubset();

    char tmp_fac[10];
    short fac_id = 0;

    // Note: This has a side affect of calling `acctsgdb_()` (see which at a
    // minimum modifies the `tsgdb_common_` global state (e.g.,
    // TSG_BR_NOTIFY_BITS)
    momtk_get_brkr_facility(ticket.prcnum, ticket.broker, ticket.dept, ticket.curr, FTPLATFORM,
                            ticket.setloc, ticket.tktnum, &fac_id, tmp_fac);
    ticket.facility = fac_id;

    // load tkt type here, not inside load_mom_tkt.
    // temporarily use tkt_type for FTRECTYP till tkt.rectype
    // is redefined
    ticket.tkttype = FTRECTYP;

    if (ticket.tktaction == EMIR_RPT) {
        return MOMSRVR_VALID_TICKET;
    }

    if (!tarlrectypok_(&ticket.tkttype)) {
        if (ticket.dept == 10 && (ticket.tkttype == XP || ticket.tkttype == CPM)) { // FX cancels
            BAEL_LOG_INFO << "prcnum=" << ticket.prcnum << ", tktnum=" << ticket.tktnum
                          << " is a FX cancel." << BAEL_LOG_END;
        } else {
            BAEL_LOG_INFO << "prcnum=" << ticket.prcnum << ", tktnum=" << ticket.tktnum
                          << " not master skipping" << BAEL_LOG_END;
            return MOMSRVR_BYPASS_TICKET;
        }
    }

    BAEL_LOG_DEBUG << "dbg mom prcnum=" << ticket.prcnum << ", tktnum=" << ticket.tktnum
                   << ", tkttype=" << ticket.tkttype << ", facility=" << ticket.facility
                   << ", action=" << actionNumberToString[ticket.tktaction]
                   << ", matchingstatus=" << ticket.matchingstatus << BAEL_LOG_END;

    if (bbit_process_rabbitmq_tradeworkfloweventsconsumer__value()) {
        // For AQI message, XPT_ALLOC_UPD is equivalent to fully allocated
        bool isFullyAllocated = ticket.tktaction == XPT_ALLOC_UPD;
        bool isAddOrXptTicket = ticket.tktaction == ADD_TKT || ticket.tktaction == XPT_ALLOC_UPD;

        if (isAddOrXptTicket) {
            FlowReturnCode rcode = getCurrentFlowStep(ticket, IsFlowStart::True);
            if (FlowReturnCode::Success != rcode && FlowReturnCode::FlowEnd != rcode) {
                if (FlowReturnCode::NoStep == rcode) {
                    BAEL_LOG_INFO << __func__ << ": no workflow matches for ticket"
                                  << " prcnum=" << ticket.prcnum << " tktnum=" << ticket.tktnum
                                  << BAEL_LOG_END;
                    return MOMSRVR_BYPASS_TICKET;
                } else {
                    BAEL_LOG_ERROR << __func__ << ": getCurrentFlowStep failed."
                                   << " rcode=" << rcode.getValue() << " prcnum=" << ticket.prcnum
                                   << " tktnum=" << ticket.tktnum
                                   << " db_error=" << d_ticketInterface->getLastTranErrorStr()
                                   << " error_type=getCurrentFlowStep_failed" << BAEL_LOG_END;
                }
                return MOMSRVR_INVALID_TICKET;
            }
            if (!checkMomTkt(ticket.prcnum, ticket.tktnum, ticket.trandate)) { // New ticket
                ticket.tktaction = ADD_TKT;

                BAEL_LOG_INFO << "adding to momdb mom_ticket_detail"
                              << " tktnum=" << ticket.tktnum << " prcnum=" << ticket.prcnum
                              << BAEL_LOG_END;
                if (d_ticketInterface->addTktDetail(ticket)) {
                    BAEL_LOG_ERROR << "addTktDetail() failed."
                                   << " tktnum=" << ticket.tktnum << " prcnum=" << ticket.prcnum
                                   << " db_error=" << d_ticketInterface->getLastTranErrorStr()
                                   << " error_type=addTktDetail_failed" << BAEL_LOG_END;
                    return MOMSRVR_INVALID_TICKET;
                }
            } else {
                short allocatedStatus;
                if (d_ticketInterface->getTktAllocatedStatus(ticket, allocatedStatus)) {
                    BAEL_LOG_ERROR << __func__ << ": getTktAllocatedStatus() failed."
                                   << " tktnum=" << ticket.tktnum << " prcnum=" << ticket.prcnum
                                   << " db_error=" << d_ticketInterface->getLastTranErrorStr()
                                   << " error_type=getTktAllocatedStatus_failed" << BAEL_LOG_END;
                    return MOMSRVR_INVALID_TICKET;
                }
                if (allocatedStatus) {
                    BAEL_LOG_INFO << __func__ << ": already fully allocated skip"
                                  << " tktnum=" << ticket.tktnum << " prcnum=" << ticket.prcnum
                                  << BAEL_LOG_END;
                    return MOMSRVR_BYPASS_TICKET;
                }
            }

            if (isFullyAllocated) {
                if (d_ticketInterface->updateTktflowToAllocatedStatus(ticket)) {
                    BAEL_LOG_ERROR << "updateTktflowToAllocatedStatus()) failed."
                                   << " tktnum=" << ticket.tktnum << " prcnum=" << ticket.prcnum
                                   << " db_error=" << d_ticketInterface->getLastTranErrorStr()
                                   << " error_type=updateTktflowToAllocatedStatus_failed"
                                   << BAEL_LOG_END;
                    return MOMSRVR_INVALID_TICKET;
                }
            }
        }
    }
    return MOMSRVR_VALID_TICKET;
}

int momsrvr_business::checkCurrentStepStatus(momtkt& ticket)
{
    using namespace aimcommon;
    BAEL_LOG_SET_CATEGORY("M_MOMSRVR.MOMSRVR_BUSINESS.CHECKCURRENTSTEPSTATUS");

    bool isNewTicket = ticket.tktaction == ADD_TKT;
    FlowReturnCode rcode = getCurrentFlowStep(ticket, isNewTicket);
    if (rcode == FlowReturnCode::NoStep) {
        return MOMSRVR_BYPASS_TICKET;
    } else if (rcode != FlowReturnCode::Success && rcode != FlowReturnCode::FlowEnd) {
        BAEL_LOG_ERROR << __func__ << ": getCurrentFlowStep failed."
                       << " prcnum=" << ticket.prcnum << " tktnum=" << ticket.tktnum
                       << " error_type=getCurrentFlowStep_failed" << BAEL_LOG_END;
        return MOMSRVR_INVALID_TICKET;
    }

    bool stepReleased;
    bool readyForNextStep;
    if (!getTicketStepStatus(ticket, makeOutParam(stepReleased), makeOutParam(readyForNextStep))) {
        BAEL_LOG_ERROR << __func__ << ": getTicketStepStatus failed."
                       << " prcnum=" << ticket.prcnum << " tktnum=" << ticket.tktnum
                       << " error_type=getTicketStepStatus_failed" << BAEL_LOG_END;
        return MOMSRVR_INVALID_TICKET;
    }

    if (stepReleased) {
        BAEL_LOG_INFO << __func__
                      << ": Ticket current step already released. Skip. tktnum=" << ticket.tktnum
                      << " prcnum=" << ticket.prcnum << " curr_state" << ticket.curr_state
                      << BAEL_LOG_END;
        return MOMSRVR_BYPASS_TICKET;
    }
    return MOMSRVR_VALID_TICKET;
}

bool momsrvr_business::process(momtkt& ticket)
{
    BAEL_LOG_SET_CATEGORY("M_MOMSRVR.MOMSRVR_BUSINESS.PROCESS");
    const char* GUTS_VALUE_FUNCTION_NAME = "process";

    int rcode = prepareMomProcess(ticket);
    if (rcode) {
        if (rcode == MOMSRVR_BYPASS_TICKET) {
            return true;
        }
        BAEL_LOG_INFO << __func__ << ": Invalid ticket prcnum=" << ticket.prcnum
                      << " tktnum=" << ticket.tktnum << BAEL_LOG_END;
        return false;
    }
    BAEL_LOG_INFO << "prcnum=" << ticket.prcnum << " tktnum=" << ticket.tktnum
                  << " action=" << actionNumberToString[ticket.tktaction]
                  << " processing_status=start" << BAEL_LOG_END;

    GUTZ_METRICS_TAGGED_COUNTER(GUTS_NAMESPACE_ROOT.c_str(), GUTS_METRIC_MOMSRVR_FUNCTION.c_str(),
                                1, GUTS_TAG_FUNCTION_NAME.c_str(), GUTS_VALUE_FUNCTION_NAME,
                                GUTS_TAG_EVENT_TYPE.c_str(), bsl::to_string(ticket.tktaction),
                                GUTS_TAG_PRICING_NUMBER.c_str(), bsl::to_string(ticket.prcnum));
    GUTZ_METRICS_TAGGED_DISTRIBUTION_TIMER_MS(
        GUTS_NAMESPACE_ROOT.c_str(), GUTS_METRIC_MOMSRVR_FUNCTION.c_str(),
        GUTS_TAG_FUNCTION_NAME.c_str(), GUTS_VALUE_FUNCTION_NAME, GUTS_TAG_PRICING_NUMBER.c_str(),
        bsl::to_string(ticket.prcnum), GUTS_TAG_EVENT_TYPE.c_str(),
        bsl::to_string(ticket.tktaction));

    switch (ticket.tktaction) {
    case ADD_TKT:
        if (bbit_process_rabbitmq_tradeworkfloweventsconsumer__value()) {
            rcode = checkCurrentStepStatus(ticket);
            if (rcode) {
                return (rcode == MOMSRVR_BYPASS_TICKET);
            }
        }
        rcode = processNewTkt(ticket);
        break;

    // cxl/corr from momtksvc or state update from other MOM apps
    case UPD_TKT:
        if (ticket.tkttype == XTT || ticket.tkttype == XP) {
            processCxlCorrTkt(ticket);
        } else {
            // offline update
            BAEL_LOG_INFO << "recvd buf " << ticket.toMessageBuf(ticket) << BAEL_LOG_END;

            if (bbit_momsrvr_using_flowbot__value()) {
                FlowReturnCode rc = processUpdateTicket(ticket);
                if (rc == FlowReturnCode::Error) {
                    BAEL_LOG_ERROR << __func__ << ": processUpdateTicket failed."
                                   << " rcode=" << rc.getValue() << " prcnum=" << ticket.prcnum
                                   << " tktnum=" << ticket.tktnum
                                   << " error_type=processUpdateTicket_failed" << BAEL_LOG_END;
                    rcode = rc.getValue();
                } else {
                    rcode = processTktFlow(ticket);
                }
            } else {
                rcode = processActiveTkt(ticket);
            }
        }
        break;

    /* update from XPT on alloc. complete
       autorelease alloc. or settlement, whichever
       is first in the flow */
    case XPT_ALLOC_UPD:
        if (bbit_process_rabbitmq_tradeworkfloweventsconsumer__value()) {
            rcode = checkCurrentStepStatus(ticket);
            if (rcode) {
                return (rcode == MOMSRVR_BYPASS_TICKET);
            }
            rcode = processTktFlow(ticket);
        } else {
            rcode = processAllocationUpdate(ticket);
        }
        break;

    case EMIR_RPT:
        processReporting(ticket);

        break;
    default:
        break;
    }

    if (FlowReturnCode::Success != rcode && FlowReturnCode::FlowEnd != rcode) {
        BAEL_LOG_ERROR << "Failed to process ticket: "
                       << " rcode=" << rcode << " prcnum=" << ticket.prcnum
                       << " tktnum=" << ticket.tktnum
                       << " action=" << actionNumberToString[ticket.tktaction]
                       << " processing_status=end" << BAEL_LOG_END;
        return false;
    } else {
        BAEL_LOG_INFO << "Ticket processing succeeded: "
                      << " prcnum=" << ticket.prcnum << " tktnum=" << ticket.tktnum
                      << " action=" << actionNumberToString[ticket.tktaction]
                      << " processing_status=end" << BAEL_LOG_END;
        return true;
    }
}

int momsrvr_business::processActiveTkt(momtkt& ticket)
{
    using namespace aimcommon;

    BAEL_LOG_SET_CATEGORY("M_MOMSRVR.MOMSRVR_BUSINESS.PROCESSACTIVETKT");

    FlowReturnCode rcode = FlowReturnCode::Success;

    ticket.matchingstatus = 0;

    BAEL_LOG_INFO << __func__ << ": prcnum=" << ticket.prcnum << " tktnum=" << ticket.tktnum
                  << " product=" << ticket.product << BAEL_LOG_END;

    rcode = moveToNextFlowStep(makeOutParam<momtkt>(ticket), MOM_STATE_COMPLETE);
    if (FlowReturnCode::Error == rcode) {
        BAEL_LOG_ERROR << __func__ << ": moveToNextFlowStep failed at processActiveTkt."
                       << " rcode=" << rcode.getValue() << " prcnum=" << ticket.prcnum
                       << " tktnum=" << ticket.tktnum << " error_type=cannot_move_to_next_flow_step"
                       << BAEL_LOG_END;
    }

    rcode = getFirmFlow(makeOutParam(ticket), IsFlowStart::False);

    if (FlowReturnCode::Success != rcode && FlowReturnCode::FlowEnd != rcode) {
        BAEL_LOG_ERROR << __func__ << ": getFirmFlow failed."
                       << " rcode=" << rcode.getValue() << " prcnum=" << ticket.prcnum
                       << " tktnum=" << ticket.tktnum
                       << " db_error=" << d_ticketInterface->getLastTranErrorStr()
                       << " error_type=getFirmFlow_failed" << BAEL_LOG_END;
    } else if (FlowReturnCode::FlowEnd == rcode) {
        BAEL_LOG_INFO << __func__ << ": Reached the end of the flow."
                      << " prcnum=" << ticket.prcnum << " tktnum=" << ticket.tktnum << BAEL_LOG_END;

        rcode = FlowReturnCode::Success;
    } else {
        BAEL_LOG_INFO << __func__ << ": Successfully retrieved flow."
                      << " curr_state=" << ticket.curr_state << " next_state=" << ticket.next_state
                      << " tktnum=" << ticket.tktnum << " prcnum=" << ticket.prcnum << BAEL_LOG_END;

        // At each step, ticket.curr_state determines what the action should be
        // And processActiveTkt() is responsible for rolling the ticket to the
        // next state by setting ticket.curr_state to ticket.next_state so that
        // processTktFlow will choose the correct action to perform

        // And if the next_state is the end of the flow(0), it means that we've
        // performed the last step so we can early exit with success return code
        if (!bbit_momsrvr_using_flowbot__value()) {
            ticket.curr_state = ticket.next_state;
        }

        rcode = convertToFlowReturnCode(processTktFlow(ticket));

        if (FlowReturnCode::Success != rcode) {
            BAEL_LOG_ERROR << __func__ << ": processTktFlow failed. rcode=" << rcode.getValue()
                           << " prcnum=" << ticket.prcnum << " tktnum=" << ticket.tktnum
                           << " error_type=ticket_processing_failed" << BAEL_LOG_END;
        }
    }

    return rcode.getValue();
}

FlowReturnCode momsrvr_business::processUpdateTicket(momtkt& ticket)
{
    BAEL_LOG_SET_CATEGORY("M_MOMSRVR.MOMSRVR_BUSINESS.PROCESSUPDATETICKET");

    // ticket's curr_state is set to the step that changed status in this
    // UPD_TKT We record this by changing the step status to MOM_STATE_COMPLETE
    FlowReturnCode rcode = convertToFlowReturnCode(updStepStatus(ticket, MOM_STATE_COMPLETE));
    if (rcode == FlowReturnCode::Error) {
        BAEL_LOG_ERROR << __func__ << ": updStepStatus failed"
                       << " rcode=" << rcode.getValue() << " prcnum=" << ticket.prcnum
                       << " tktnum=" << ticket.tktnum << " error_type=updStepStatus_failed"
                       << BAEL_LOG_END;
        return rcode;
    }

    // We must set curr_state to the real current step of the ticket in
    // the workflow, since we want to start/resume processing from there
    rcode = getFirmFlow(aimcommon::makeOutParam(ticket), IsFlowStart::False);
    if (rcode == FlowReturnCode::Error) {
        BAEL_LOG_ERROR << __func__ << ": getFirmFlow failed."
                       << " rcode=" << rcode.getValue() << " prcnum=" << ticket.prcnum
                       << " tktnum=" << ticket.tktnum << " error_type=getFirmFlow_failed"
                       << BAEL_LOG_END;
    } else {
        BAEL_LOG_INFO << __func__ << ": Successfully retrieved flow."
                      << " curr_state=" << ticket.curr_state << " next_state=" << ticket.next_state
                      << " tktnum=" << ticket.tktnum << " prcnum=" << ticket.prcnum << BAEL_LOG_END;
    }

    return rcode;
}

int momsrvr_business::processCxlCorrTkt(momtkt& ticket)
{
    BAEL_LOG_SET_CATEGORY("M_MOMSRVR.MOMSRVR_BUSINESS.PROCESSCXLCORRTKT");
    int curr_state = 1, rcode = 0;

    momtkt orig;
    orig.tktnum = ticket.cxlnum;
    orig.prcnum = ticket.prcnum;
    orig.trandate = ticket.trade_date;
    orig.curr_state = curr_state;

    if (rcode)
        return 3;

    updateMasterFlow(orig);

    rcode = getUtils().sendTktOut(orig);

    return rcode;
}

int momsrvr_business::updateMasterFlow(momtkt& ticket)
{
    using namespace aimcommon;

    BAEL_LOG_SET_CATEGORY("M_MOMSRVR.MOMSRVR_BUSINESS.UPDATEMASTERFLOW");

    FlowReturnCode rcode = FlowReturnCode::Success;

    if (!checkMomTkt(ticket.prcnum, ticket.tktnum, ticket.trandate)) {
        BAEL_LOG_ERROR << __func__ << ": checkMomTkt failed"
                       << " prcnum=" << ticket.prcnum << " tktnum=" << ticket.tktnum
                       << " error_type=checkTkt_failed" << BAEL_LOG_END;
        return FlowReturnCode::Error.getValue();
    }

    BAEL_LOG_INFO << __func__ << ": prcnum=" << ticket.prcnum << ", tktnum=" << ticket.tktnum
                  << ", product=" << ticket.product << BAEL_LOG_END;

    rcode = getFirmFlow(makeOutParam(ticket), IsFlowStart::False);

    if (FlowReturnCode::Success != rcode) {
        BAEL_LOG_ERROR << __func__ << ": getFirmFlow failed."
                       << " rcode=" << rcode.getValue() << " prcnum=" << ticket.prcnum
                       << " tktnum=" << ticket.tktnum
                       << " db_error=" << d_ticketInterface->getLastTranErrorStr()
                       << " error_type=getFirmFlow_failed" << BAEL_LOG_END;
        return rcode.getValue();
    }

    BAEL_LOG_INFO << __func__ << ": curr_state=" << ticket.curr_state
                  << ", next_state=" << ticket.next_state << BAEL_LOG_END;

    rcode = convertToFlowReturnCode(updStepStatus(ticket, MOM_STATE_IN_PROGRESS));

    if (FlowReturnCode::Success != rcode) {
        BAEL_LOG_ERROR << __func__ << ": updStepStatus failed."
                       << " prcnum=" << ticket.prcnum << " tktnum=" << ticket.tktnum
                       << " rcode=" << rcode.getValue()
                       << " db_error=" << d_ticketInterface->getLastTranErrorStr()
                       << " error_type=updStepStatus_failed" << BAEL_LOG_END;
        return rcode.getValue();
    }

    // Previously the code passes facility id = 0 into updateTktflow
    // which will set the ticket reference's facility to 0.
    // On the other occasion where updateTktflow() is called,
    // it doesn't change the ticket's facility,
    // so we took out the facility id from the parameter list
    // and we just manually set the ticket's facility to 0 here before we pass
    // it into moveToNextFlowStep()
    ticket.facility = 0;

    rcode = moveToNextFlowStep(makeOutParam<momtkt>(ticket), MOM_STATE_IN_PROGRESS);

    if (FlowReturnCode::Success != rcode) {
        BAEL_LOG_ERROR << __func__ << ": moveToNextFlowStep failed."
                       << " prcnum=" << ticket.prcnum << " tktnum=" << ticket.tktnum
                       << " rcode=" << rcode.getValue()
                       << " db_error=" << d_ticketInterface->getLastTranErrorStr()
                       << " error_type=cannot_move_to_next_flow_step" << BAEL_LOG_END;
        return rcode.getValue();
    }

    return rcode.getValue();
}

bool momsrvr_business::checkMomTkt(int prcnum, int tktnum, int trandate)
{
    BAEL_LOG_SET_CATEGORY("M_MOMSRVR.MOMSRVR_BUSINESS.CHECKMOMTKT");
    momtkt tkt;

    tkt.prcnum = prcnum;
    tkt.tktnum = tktnum;
    tkt.trandate = trandate;

    bool ret = d_ticketInterface->checkTkt(tkt);
    if (false == ret) {
        BAEL_LOG_INFO << __func__ << ": not able to find ticket in the database"
                      << " prcnum=" << prcnum << " tktnum=" << tktnum
                      << " db_error=" << d_ticketInterface->getLastTranErrorStr() << BAEL_LOG_END;
    }
    return ret;
}

void momsrvr_business::setTktInterface(momtkt_TktInterface* dbi)
{
    d_ticketInterface = dbi;
}

bool momsrvr_business::validTCTMTkt(momtkt& ticket)
{
    return (ticket.tkttype == PM || ticket.tkttype == CPM || ticket.tkttype == TT
            || ticket.tkttype == CTT ||

            ((ticket.tkttype == 1 || ticket.tkttype == 5) && btst(ticket.ftflag2, 4)
             && !btst(ticket.ftflag2, 10) && !btst(ticket.ftflag2, 11)));
}

short momsrvr_business::getDerivativeType()
{
    short deriType = aim_reporting::VCON_NONE_DERIVITIVE_TYPE;
    if (is_cds_())
        deriType = aim_reporting::VCON_CDS_TYPE;
    else if (is_swapsdb_security_())
        deriType = aim_reporting::VCON_IRS_TYPE;

    return deriType;
}

int momsrvr_business::getFirstState(int prcnum, int dept, int* stepid)
{
    BAEL_LOG_SET_CATEGORY("M_MOMSRVR.MOMSRVR_BUSINESS.GETFIRSTSTATE");
    // gets first of  (ALLOC, SETTLEMENT, TRFEED) steps
    int step = 0;
    int rc = 0;

    BAEL_LOG_INFO << "getFirstState: prcnum=" << prcnum << ", product=" << dept << BAEL_LOG_END;
    rc = d_ticketInterface->getFirstStep(prcnum, dept, &step, stepid);
    if (rc) {
        BAEL_LOG_ERROR << "getFirstState() error: prcnum=" << prcnum << " rc=" << rc
                       << " db_error=" << d_ticketInterface->getLastTranErrorStr()
                       << " error_type=getFirstStep_failed" << BAEL_LOG_END;
        return -1;
    }
    BAEL_LOG_INFO << "getFirstState: prcnum=" << prcnum << ", step=" << step
                  << ", stepid=" << *stepid << BAEL_LOG_END;

    if (step == 0) {
        BAEL_LOG_INFO << "getFirstState(): prcnum=" << prcnum << " first step is 0."
                      << " returning -1" << BAEL_LOG_END;
        return -1;
    }

    return step;
}

/*
   Process ticket for the current state in tk buffer
   if new_tkt is true ..just send out current step
   if new_tkt is false.. then it is step complete update
      so, move to the next step

 */
int momsrvr_business::processTktFlow(momtkt& ticket)
{
    using namespace aimcommon;

    BAEL_LOG_SET_CATEGORY("M_MOMSRVR.MOMSRVR_BUSINESS.PROCESSTKTFLOW");

    FlowReturnCode rcode = FlowReturnCode::Success;

    BAEL_LOG_INFO << __func__ << ": tktnum=" << ticket.tktnum
                  << ", curr_state=" << ticket.curr_state << ", next_state=" << ticket.next_state
                  << BAEL_LOG_END;

    bool stepReleased = false;
    bool readyForNextStep = false;
    if (bbit_momsrvr_using_flowbot__value()) {
        if (!getTicketStepStatus(ticket, makeOutParam<bool>(stepReleased),
                                 makeOutParam<bool>(readyForNextStep))) {
            BAEL_LOG_ERROR << __func__ << ": getTicketStepStatus failed."
                           << " prcnum=" << ticket.prcnum << " tktnum=" << ticket.tktnum
                           << " error_type=getTicketStepStatus_failed" << BAEL_LOG_END;
            return FlowReturnCode::Error.getValue();
        }
    }

    if (stepReleased) {
        BAEL_LOG_INFO << __func__ << " step already released by momsrvr"
                      << " prcnum=" << ticket.prcnum << " tktnum=" << ticket.tktnum
                      << " curr_state=" << ticket.curr_state << BAEL_LOG_END;

        if (readyForNextStep) {
            rcode = convertToFlowReturnCode(processActiveTkt(ticket));
        }
    } else if (tktManualReleased(ticket)) {
        BAEL_LOG_INFO << __func__ << " : tktnum=" << ticket.tktnum << " already released. Skipping."
                      << BAEL_LOG_END;

        rcode = convertToFlowReturnCode(processActiveTkt(ticket));
    } else if (ticket.curr_state == MOMDB_BLOCK_STATE && is_bilateral_repo(ticket.tkttype)) {
        /* Skip VCON for bilateral repos*/
        BAEL_LOG_INFO << __func__ << ": tktnum=" << ticket.tktnum
                      << " is a bilateral repo. Skip VCON." << BAEL_LOG_END;

        rcode = convertToFlowReturnCode(processActiveTkt(ticket));
    } else {
        bool autoReleaseEnabled = false;
        bool waitForStatusEnabled = false;

        rcode = getFlowFlags(ticket, makeOutParam<bool>(autoReleaseEnabled),
                             makeOutParam<bool>(waitForStatusEnabled));

        // For flowbot implementation, when the attempt to get the current step
        // returns flow end code the last step has already been executed, so it
        // necessitates an early return here.
        if (FlowReturnCode::FlowEnd == rcode && bbit_momsrvr_using_flowbot__value()) {
            return FlowReturnCode::Success.getValue();
        }

        if (FlowReturnCode::Success != rcode) {
            BAEL_LOG_ERROR << __func__ << ": getFlowflags failed"
                           << " prcnum=" << ticket.prcnum << " tktnum=" << ticket.tktnum
                           << " rcode=" << rcode.getValue()
                           << " db_error=" << d_ticketInterface->getLastTranErrorStr()
                           << " error_type=getFlowFlags_failed" << BAEL_LOG_END;
            return rcode.getValue();
        }

        BAEL_LOG_INFO << __func__ << ": dbg autoReleaseEnabled=" << autoReleaseEnabled
                      << ", waitForStatusEnabled=" << waitForStatusEnabled
                      << ", matchingstatus=" << ticket.matchingstatus << BAEL_LOG_END;

        // fot tradefeed autrls anyway but check wait
        // state to see if tasu check is needed

        if (!bbit_momsrvr_tradefeed_in_sendtktout__value()
            && MOMDB_TRADEFEED_STATE == ticket.curr_state) {
            BAEL_LOG_INFO << __func__ << ": releasing to TradeFeed"
                          << " prcnum=" << ticket.prcnum << " tktnum=" << ticket.tktnum
                          << BAEL_LOG_END;
            rcode = convertToFlowReturnCode(
                getUtils().sendToTradeFeed(ticket, waitForStatusEnabled, autoReleaseEnabled));
        } else {
            if (autoReleaseEnabled
                || (ticket.curr_state == MOMDB_TRADEFEED_STATE && waitForStatusEnabled)) {
                bool xpt_alloc_status = false;

                // if ticket allocatable then make sure that we only continue
                // (if not VCON) if it has been marked allocated
                if ((ticket.tkttype == PM || ticket.tkttype == CPM
                     || is_repo_master_tkt(ticket.tkttype))
                    && (ticket.curr_state > MOMDB_BLOCK_STATE)) {
                    short is_allocated;

                    rcode = convertToFlowReturnCode(
                        d_ticketInterface->getTktAllocatedStatus(ticket, is_allocated));

                    if (FlowReturnCode::Success != rcode) {
                        BAEL_LOG_ERROR << __func__ << ": getTktAllocatedStatus failed."
                                       << " rcode=" << rcode.getValue()
                                       << " prcnum=" << ticket.prcnum << " tktnum=" << ticket.tktnum
                                       << " db_error=" << d_ticketInterface->getLastTranErrorStr()
                                       << " error_type=getTktAllocatedStatus_failed"
                                       << BAEL_LOG_END;
                        return rcode.getValue();
                    }

                    // FIXME: when cleaning up this breg, also remove the
                    // declaration above If we do not bypass the temporary
                    // allocated status logic then we need to consider
                    // xpt_alloc_status together with is_allocated to determine
                    // the status of being fully allocated for the ticket
                    if (!bbit_mom_flow_remove_temp_allocated_status__value() && !is_allocated) {
                        xpt_alloc_status = d_ticketInterface->checkAllocStatus(ticket);
                    }

                    BAEL_LOG_INFO << __func__ << ": is_allocated=" << is_allocated
                                  << " received xpt_alloc_status=" << xpt_alloc_status
                                  << " for prcnum=" << ticket.prcnum
                                  << " and tktnum=" << ticket.tktnum << BAEL_LOG_END;

                    if (!is_allocated
                        && (!xpt_alloc_status
                            || bbit_mom_flow_remove_temp_allocated_status__value())) {
                        BAEL_LOG_INFO << __func__ << ": Not fully allocated. wait for xpt. "
                                      << "prcnum=" << ticket.prcnum << ", tktnum=" << ticket.tktnum
                                      << ", tkttype=" << ticket.tkttype
                                      << ", curr_state=" << ticket.curr_state << BAEL_LOG_END;
                        return FlowReturnCode::Success.getValue();
                    }
                }

                if (!bbit_mom_flow_remove_temp_allocated_status__value() && xpt_alloc_status) {
                    rcode = convertToFlowReturnCode(d_ticketInterface->removeAllocStatus(ticket));
                    if (FlowReturnCode::Success != rcode) {
                        BAEL_LOG_ERROR << __func__ << ": removeAllocStatus failed."
                                       << " rcode=" << rcode.getValue()
                                       << " tktnum=" << ticket.tktnum << " prcnum=" << ticket.prcnum
                                       << " error_type=cannot_move_to_next_flow_step"
                                       << BAEL_LOG_END;
                    }
                }

                if (ticket.curr_state == MOMDB_TRADEFEED_STATE && waitForStatusEnabled) {
                    if (getUtils().isFirmReleaseSet(ticket)) {
                        rcode = convertToFlowReturnCode(getUtils().sendTktOut(ticket));
                    } else {
                        // TARL settings say not to autorelease to tradefeed, so
                        // we skip the step
                        ticket.matchingstatus = 1;
                        rcode = FlowReturnCode::Success;
                    }
                } else {
                    GUTZ_METRICS_TAGGED_DISTRIBUTION_TIMER_MS(
                        GUTS_NAMESPACE_ROOT.c_str(), GUTS_METRIC_MOMSRVR_FUNCTION.c_str(),
                        GUTS_TAG_FUNCTION_NAME.c_str(), "sendTktOut", GUTS_TAG_EVENT_TYPE.c_str(),
                        bsl::to_string(ticket.curr_state));
                    rcode = convertToFlowReturnCode(getUtils().sendTktOut(ticket));
                }

                if (FlowReturnCode::Success != rcode) {
                    BAEL_LOG_ERROR << __func__ << ": sendTktOut failed."
                                   << " rcode=" << rcode.getValue()
                                   << " curr_state=" << ticket.curr_state
                                   << " tktnum=" << ticket.tktnum << " prcnum=" << ticket.prcnum
                                   << " error_type=sendTktOut_failed" << BAEL_LOG_END;
                }
            }
        }

        int step_status;
        if (bbit_momsrvr_using_flowbot__value()) {
            step_status = (FlowReturnCode::Success == rcode) ? MOM_STATE_IN_PROGRESS
                                                             : MOM_STATE_NOT_SENT;
        } else {
            step_status = (FlowReturnCode::Success == rcode) ? MOM_STATE_COMPLETE
                                                             : MOM_STATE_IN_PROGRESS;
        }

        if (updStepStatus(ticket, step_status)) {
            BAEL_LOG_ERROR << __func__ << ": updStepStatus failed."
                           << " tktnum=" << ticket.tktnum << " prcnum=" << ticket.prcnum
                           << " curr_state=" << ticket.curr_state
                           << " error_type=updStepStatus_failed" << BAEL_LOG_END;
        }

        // Jump to next step only
        // if set by the prev. state (matchingstatus = 1) (OR)
        // configured as 'no wait' in operational flow
        if ((FlowReturnCode::FlowEnd != rcode) && (rcode == FlowReturnCode::Success)
            && (ticket.matchingstatus == 1 || !waitForStatusEnabled)) {
            rcode = convertToFlowReturnCode(processActiveTkt(ticket));
        }
    }

    return rcode.getValue();
}

int momsrvr_business::processNewTkt(momtkt& ticket)
{
    BAEL_LOG_SET_CATEGORY("M_MOMSRVR.MOMSRVR_BUSINESS.PROCESSNEWTKT");

    const bool split_add_tkt_emir_rpt_breg = bbit_add_tkt_during_xpt_alloc_upd__value();

    if (!split_add_tkt_emir_rpt_breg
        && (bbit_send_all_trades_to_emir_trigger__value() || isEmirEnabled(ticket))) {
        emirSSIValues ssiInfo;
        string jurisdiction;

        BAEL_LOG_INFO << __func__ << ": checking reporting"
                      << " prcnum=" << ticket.prcnum << " tktnum=" << ticket.tktnum
                      << " jurisdiction=EMIR" << BAEL_LOG_END;

        if (isEmirEligible(ticket, ssiInfo)) {
            jurisdiction = "EMIR";
            BAEL_LOG_INFO << __func__ << ": eligible for reporting"
                          << " prcnum=" << ticket.prcnum << " tktnum=" << ticket.tktnum
                          << " jurisdiction=EMIR" << BAEL_LOG_END;

            track_user_hit_no_parmcm(554997, 0, P6UUID);

            if (reportTradeToBSTP(ticket, ssiInfo, jurisdiction)) {
                BAEL_LOG_ERROR << __func__ << ": reporting failed"
                               << " prcnum=" << ticket.prcnum << " tktnum=" << ticket.tktnum
                               << " jurisdiction=EMIR"
                               << " error_type=momsrvr_failed_to_report" << BAEL_LOG_END;
            }
        } else {
            BAEL_LOG_INFO << __func__ << ": not eligible for reporting"
                          << " prcnum=" << ticket.prcnum << " tktnum=" << ticket.tktnum
                          << " jurisdiction=EMIR" << BAEL_LOG_END;
        }

        if (isMasEligible(ticket, ssiInfo)) {
            jurisdiction = "MAS";
            BAEL_LOG_INFO << __func__ << ": eligible for reporting"
                          << " prcnum=" << ticket.prcnum << " tktnum=" << ticket.tktnum
                          << " jurisdiction=MAS" << BAEL_LOG_END;

            track_user_hit_no_parmcm(778958, 0, P6UUID);

            if (reportTradeToBSTP(ticket, ssiInfo, jurisdiction)) {
                BAEL_LOG_ERROR << __func__ << ": reporting failed"
                               << " prcnum=" << ticket.prcnum << " tktnum=" << ticket.tktnum
                               << " jurisdiction=MAS"
                               << " error_type=momsrvr_failed_to_report" << BAEL_LOG_END;
            }
        } else {
            BAEL_LOG_INFO << __func__ << ": not eligible for reporting"
                          << " prcnum=" << ticket.prcnum << " tktnum=" << ticket.tktnum
                          << " jurisdiction=MAS" << BAEL_LOG_END;
        }

        if (isASICEligible(ticket, ssiInfo)) {
            jurisdiction = "ASIC";
            BAEL_LOG_INFO << __func__ << ": eligible for reporting"
                          << " prcnum=" << ticket.prcnum << " tktnum=" << ticket.tktnum
                          << " jurisdiction=ASIC" << BAEL_LOG_END;

            if (reportTradeToBSTP(ticket, ssiInfo, jurisdiction)) {
                BAEL_LOG_ERROR << __func__ << ": reporting failed"
                               << " prcnum=" << ticket.prcnum << " tktnum=" << ticket.tktnum
                               << " jurisdiction=ASIC"
                               << " error_type=momsrvr_failed_to_report" << BAEL_LOG_END;
            }
        } else {
            BAEL_LOG_INFO << __func__ << ": not eligible for reporting"
                          << " prcnum=" << ticket.prcnum << " tktnum=" << ticket.tktnum
                          << " jurisdiction=ASIC" << BAEL_LOG_END;
        }
    }

    if (notMomFlowTkt(ticket)) {
        // Not an error
        return 0;
    }

    if (!initMomTktFlow(ticket)) {
        BAEL_LOG_INFO << __func__ << ": initMomTktFlow failed"
                      << " prcnum=" << ticket.prcnum << " tktnum=" << ticket.tktnum << BAEL_LOG_END;
        return 1;
    }

    ticket.matchingstatus = 0; // Reset matching status before further actions

    return processTktFlow(ticket);
}

bool momsrvr_business::getTicketStepStatus(momtkt& ticket, aimcommon::OutParam<bool> stepReleased,
                                           aimcommon::OutParam<bool> readyForNextStep)
{
    using namespace aimcommon;

    BAEL_LOG_SET_CATEGORY("M_MOMSRVR.MOMSRVR_BUSINESS.GETTICKETSTEPSTATUS");

    setOutParam(stepReleased) = false;
    setOutParam(readyForNextStep) = false;

    int rc;
    {
        GUTZ_METRICS_TAGGED_DISTRIBUTION_TIMER_MS(
            GUTS_NAMESPACE_ROOT.c_str(), GUTS_METRIC_MOMSRVR_DBQUERY.c_str(),
            GUTS_TAG_FUNCTION_NAME.c_str(), "getTktDetailStatuses");
        rc = d_ticketInterface->getTktDetailStatuses(ticket);
    }
    if (rc) {
        BAEL_LOG_ERROR << __func__ << ": Error getting ticket detail status"
                       << " tktnum=" << ticket.tktnum << " prcnum=" << ticket.prcnum
                       << " dberror=" << d_ticketInterface->getLastTranErrorStr()
                       << " error_type=failed_getting_ticket_detail_status" << BAEL_LOG_END;
        return false;
    }

    bool autoRelease = false;
    bool waitForStatus = false;
    FlowReturnCode rcode = getFlowFlags(ticket, makeOutParam<bool>(autoRelease),
                                        makeOutParam<bool>(waitForStatus));
    if (rcode != FlowReturnCode::Success) {
        BAEL_LOG_ERROR << __func__ << ": Error getting flow flags"
                       << " tktnum=" << ticket.tktnum << " prcnum=" << ticket.prcnum
                       << " error_type=getFlowFlags_failed" << BAEL_LOG_END;
        return false;
    }

    int stepStatus = 0;
    if (!getCurrentTktStepStatus(ticket, makeOutParam<int>(stepStatus))) {
        return false;
    }

    bool released = stepStatus != MOM_STATE_NOT_SENT;

    setOutParam(stepReleased) = released;
    setOutParam(readyForNextStep) = (!waitForStatus && released)
        || (waitForStatus && stepStatus == MOM_STATE_COMPLETE);

    return true;
}

bool momsrvr_business::tktManualReleased(momtkt& ticket)
{
    BAEL_LOG_SET_CATEGORY("M_MOMSRVR.MOMSRVR_BUSINESS.TKTMANUALRELEASED");
    int rc = 0;
    /*
       If ticket detail table has been updated
       for the current state, then it must have been
       manually released
     */

    {
        GUTZ_METRICS_TAGGED_DISTRIBUTION_TIMER_MS(
            GUTS_NAMESPACE_ROOT.c_str(), GUTS_METRIC_MOMSRVR_DBQUERY.c_str(),
            GUTS_TAG_FUNCTION_NAME.c_str(), "getTktDetailStatuses");
        rc = d_ticketInterface->getTktDetailStatuses(ticket);
    }
    if (rc) {
        BAEL_LOG_ERROR << __func__ << ": Error getting ticket detail status"
                       << " tktnum=" << ticket.tktnum << " prcnum=" << ticket.prcnum
                       << " getTktDetailStatuses db_error="
                       << d_ticketInterface->getLastTranErrorStr()
                       << " error_type=failed_getting_ticket_detail_status" << BAEL_LOG_END;
        return false;
    }

    return getStepStatus(ticket);
}

int momsrvr_business::updStepStatus(momtkt& ticket, int status)
{
    BAEL_LOG_SET_CATEGORY("M_MOMSRVR.MOMSRVR_BUSINESS.UPDSTEPSTATUS");
    string status_col;

    switch (ticket.curr_state) {
    case MOMDB_BLOCK_STATE:
        status_col = "block_status";
        break;

    case MOMDB_ALLOCATIONS_STATE:
        status_col = "alloc_status";
        break;

    case MOMDB_TRADEFEED_STATE:
        status_col = "tradfeed_status";
        break;

    case MOMDB_SETTLEMENT_STATE:
        status_col = "settlemt_status";
        break;

    default:
        break;
    }

    int rc;
    {
        GUTZ_METRICS_TAGGED_DISTRIBUTION_TIMER_MS(
            GUTS_NAMESPACE_ROOT.c_str(), GUTS_METRIC_MOMSRVR_DBQUERY.c_str(),
            GUTS_TAG_FUNCTION_NAME.c_str(), "updateTktDetail");
        rc = d_ticketInterface->updateTktDetail(ticket, status_col, status);
    }

    if (rc) {
        BAEL_LOG_ERROR << "updateTktDetail failed for tktnum=" << ticket.tktnum
                       << " prcnum=" << ticket.prcnum
                       << " error: " << d_ticketInterface->getLastTranErrorStr()
                       << " error_type=updateTktDetail_failed" << BAEL_LOG_END;
    } else {
        BAEL_LOG_INFO << "updateTktDetail succeeded for tktnum=" << ticket.tktnum
                      << ", prcnum=" << ticket.prcnum << ", curr_state=" << ticket.curr_state
                      << ", status=" << status << BAEL_LOG_END;
    }
    return rc;
}

bool momsrvr_business::notMomFlowTkt(const momtkt& ticket)
{
    BAEL_LOG_SET_CATEGORY("M_MOMSRVR.MOMSRVR_BUSINESS.NOTMOMFLOWTKT");

    if (ticket.dept == 10 && (ticket.tkttype == XP || ticket.tkttype == CPM)) { // FX Cancel
        BAEL_LOG_INFO << "prcnum=" << ticket.prcnum << ", tktnum=" << ticket.tktnum
                      << " is a FX cancel." << BAEL_LOG_END;
    } else {
        short rectype = ticket.tkttype;
        if (!tarlrectypok_(&rectype)) {
            BAEL_LOG_INFO << "prcnum=" << ticket.prcnum << ", tktnum=" << ticket.tktnum
                          << " not master skipping" << BAEL_LOG_END;
            return true;
        }
    }

    if (!bbit_enable_ctm_autorelease_in_tc_flow__value() && ticket.facility == ALLOC_DEST_CTM) {
        BAEL_LOG_INFO << "processNewTkt: prcnum=" << ticket.prcnum << ", tktnum=" << ticket.tktnum
                      << " is destined for CTM, not a momflow ticket, exiting" << BAEL_LOG_END;
        return true;
    }

    if (btst(FTFLAG2, 2)) {
        BAEL_LOG_INFO << "processNewTkt: prcnum=" << ticket.prcnum << ", tktnum=" << ticket.tktnum
                      << " is a post trade date cancel ticket, not a momflow "
                         "ticket, exiting"
                      << BAEL_LOG_END;
        return true;
    }

    if (bbit_use_actionsv_for_momsrvr__value() && btst(FTFLAG2, 12)) {
        BAEL_LOG_INFO << "processNewTkt: prcnum=" << ticket.prcnum << ", tktnum=" << ticket.tktnum
                      << " is an allocation ticket, not momflow ticket, exiting" << BAEL_LOG_END;
        return true;
    }

    return false;
}

bool momsrvr_business::initMomTktFlow(momtkt& ticket)
{
    using namespace aimcommon;

    BAEL_LOG_SET_CATEGORY("M_MOMSRVR.MOMSRVR_BUSINESS.INITMOMTKTFLOW");

    FlowReturnCode rcode = FlowReturnCode::Success;

    BAEL_LOG_INFO << __func__ << ": prcnum=" << ticket.prcnum << " tktnum=" << ticket.tktnum
                  << " product=" << ticket.product << BAEL_LOG_END;

    rcode = getFirmFlow(makeOutParam(ticket), IsFlowStart::True);

    BAEL_LOG_INFO << __func__ << ": curr_state=" << ticket.curr_state
                  << ", next_state=" << ticket.next_state << BAEL_LOG_END;

    if (FlowReturnCode::Success != rcode && FlowReturnCode::FlowEnd != rcode) {
        if (FlowReturnCode::NoStep == rcode) {
            BAEL_LOG_INFO << __func__ << ": no workflow matches for ticket"
                          << " prcnum=" << ticket.prcnum << " tktnum=" << ticket.tktnum
                          << BAEL_LOG_END;
        } else {
            BAEL_LOG_ERROR << __func__ << ": getFirmFlow failed."
                           << " rcode=" << rcode.getValue() << " prcnum=" << ticket.prcnum
                           << " tktnum=" << ticket.tktnum
                           << " db_error=" << d_ticketInterface->getLastTranErrorStr()
                           << " error_type=getFirmFlow_failed" << BAEL_LOG_END;
        }
        return false;
    }

    if (!bbit_momsrvr_using_flowbot__value()) {
        {
            GUTZ_METRICS_TAGGED_DISTRIBUTION_TIMER_MS(GUTS_NAMESPACE_ROOT.c_str(),
                                                      GUTS_METRIC_MOMSRVR_DBQUERY.c_str(),
                                                      GUTS_TAG_FUNCTION_NAME.c_str(), "addTktFlow");
            rcode = convertToFlowReturnCode(d_ticketInterface->addTktFlow(
                ticket, ticket.curr_state, ticket.facility, ticket.next_state));
        }

        if (FlowReturnCode::Success != rcode) {
            BAEL_LOG_ERROR << __func__ << ": addTktFlow failed."
                           << " rcode=" << rcode.getValue() << " prcnum=" << ticket.prcnum
                           << " tktnum=" << ticket.tktnum << " curr_state=" << ticket.curr_state
                           << " next_state=" << ticket.next_state << " facility=" << ticket.facility
                           << " with ticketInterface."
                           << " db_error=" << d_ticketInterface->getLastTranErrorStr()
                           << " error_type=addTktFlow_failed" << BAEL_LOG_END;
            return false;
        }
    }

    return true;
}

bool momsrvr_business::getStepStatus(momtkt& ticket)
{
    switch (ticket.curr_state) {
    case MOMDB_BLOCK_STATE:
        if (ticket.blockstatus == MOM_BLOCK_CONFIRMED)
            return true;
        break;

    case MOMDB_ALLOCATIONS_STATE:
        if (ticket.allocstatus == MOM_ALLOC_ACCEPTED)
            return true;
        break;

    case MOMDB_TRADEFEED_STATE:
        if (ticket.tradfeedstatus == MOM_TRFEED_RELEASED)
            return true;
        break;

    case MOMDB_SETTLEMENT_STATE:
        if (ticket.settlementstatus == MOM_SETTLEMENT_ACCEPTED)
            return true;
        break;

    default:
        break;
    }
    return false;
}

IUtils& momsrvr_business::getUtils()
{
    BSLS_ASSERT(d_utils); // Assert class invariant
    return *d_utils;
}

int momsrvr_business::processAllocationUpdate(momtkt& ticket)
{
    using namespace aimcommon;

    BAEL_LOG_SET_CATEGORY("M_MOMSRVR.MOMSRVR_BUSINESS.PROCESSALLOCATIONUPDATE");

    int rcode = 0;
    {
        GUTZ_METRICS_TAGGED_DISTRIBUTION_TIMER_MS(
            GUTS_NAMESPACE_ROOT.c_str(), GUTS_METRIC_MOMSRVR_DBQUERY.c_str(),
            GUTS_TAG_FUNCTION_NAME.c_str(), "getTktDetailStatuses");
        rcode = d_ticketInterface->getTktDetailStatuses(ticket);
    }
    if (rcode) {
        BAEL_LOG_ERROR << __func__ << ": getTktDetailStatuses failed."
                       << " rcode=" << rcode << " tktnum=" << ticket.tktnum
                       << " prcnum=" << ticket.prcnum
                       << " db_error=" << d_ticketInterface->getLastTranErrorStr()
                       << " error_type=getTktDetailStatuses_failed" << BAEL_LOG_END;

        return FlowReturnCode::Error.getValue();
    }

    {
        GUTZ_METRICS_TAGGED_DISTRIBUTION_TIMER_MS(
            GUTS_NAMESPACE_ROOT.c_str(), GUTS_METRIC_MOMSRVR_DBQUERY.c_str(),
            GUTS_TAG_FUNCTION_NAME.c_str(), "updateTktflowToAllocatedStatus");
        rcode = d_ticketInterface->updateTktflowToAllocatedStatus(ticket);
    }
    if (rcode) {
        BAEL_LOG_ERROR << __func__ << ": updateTktflowToAllocatedStatus failed."
                       << " rcode=" << rcode << " tktnum=" << ticket.tktnum
                       << " prcnum=" << ticket.prcnum
                       << " db_error=" << d_ticketInterface->getLastTranErrorStr()
                       << " error_type=updateTktFlowToAllocatedStatus_failed" << BAEL_LOG_END;

        return FlowReturnCode::Error.getValue();
    }

    // Set the Current Step to the ticket so that
    // we will process current step if need be
    int curr_state_status = 0;

    if (!bbit_momsrvr_using_flowbot__value()) {
        {
            GUTZ_METRICS_TAGGED_DISTRIBUTION_TIMER_MS(
                GUTS_NAMESPACE_ROOT.c_str(), GUTS_METRIC_MOMSRVR_DBQUERY.c_str(),
                GUTS_TAG_FUNCTION_NAME.c_str(), "getTktStatus");
            rcode = d_ticketInterface->getTktStatus(ticket, ticket.curr_state, curr_state_status);
        }
        if (rcode) {
            BAEL_LOG_ERROR << __func__ << ": getTktStatus failed."
                           << " rcode=" << rcode << " tktnum=" << ticket.tktnum
                           << " prcnum=" << ticket.prcnum
                           << " db_error=" << d_ticketInterface->getLastTranErrorStr()
                           << " error_type=getTktStatus_failed" << BAEL_LOG_END;
            return FlowReturnCode::Error.getValue();
        }
    }

    BAEL_LOG_INFO << __func__ << ": getTktStatus: "
                  << "prcnum=" << ticket.prcnum << ", tktnum=" << ticket.tktnum
                  << ", product=" << ticket.product << ", curr_state=" << ticket.curr_state
                  << ", curr_state_status=" << curr_state_status << BAEL_LOG_END;

    if (curr_state_status != MOM_STATE_COMPLETE) {
        if (!bbit_mom_flow_remove_temp_allocated_status__value()) {
            int rcodeAllocStatus = d_ticketInterface->removeAllocStatus(ticket);
            if (rcodeAllocStatus) {
                BAEL_LOG_WARN << __func__ << ": removeAllocStatus failed."
                              << " prcnum=" << ticket.prcnum << " tktnum=" << ticket.tktnum
                              << " rcode=" << rcodeAllocStatus
                              << " db_error=" << d_ticketInterface->getLastTranErrorStr()
                              << " error_type=removeAllocStatus_failed" << BAEL_LOG_END;
            }
        }
    }

    rcode = getFirmFlow(makeOutParam<momtkt>(ticket), IsFlowStart::False).getValue();
    if (rcode != FlowReturnCode::Success.getValue()
        && rcode != FlowReturnCode::FlowEnd.getValue()) {
        BAEL_LOG_ERROR << __func__ << ": getFirmFlow failed."
                       << " rcode=" << rcode << " prcnum=" << ticket.prcnum
                       << " tktnum=" << ticket.tktnum
                       << " db_error=" << d_ticketInterface->getLastTranErrorStr()
                       << " error_type=getFirmFlow_failed" << BAEL_LOG_END;
        return rcode;
    }

    BAEL_LOG_INFO << __func__ << ": getFirmFlow:"
                  << " prcnum=" << ticket.prcnum << " tktnum=" << ticket.tktnum
                  << " curr_state=" << ticket.curr_state << " next_state=" << ticket.next_state
                  << BAEL_LOG_END;

    if (bbit_skip_released_step_for_xpt_alloc__value()) {
        int stepStatus = 0;
        if (!getCurrentTktStepStatus(ticket, makeOutParam<int>(stepStatus))) {
            return FlowReturnCode::Error.getValue();
        }

        if (stepStatus != MOM_STATE_NOT_SENT) {
            BAEL_LOG_INFO << "The current step was processed, skipping. stepStatus=" << stepStatus
                          << " curr_state=" << ticket.curr_state << " tktnum=" << ticket.tktnum
                          << " prcnum=" << ticket.prcnum << BAEL_LOG_END;
            return FlowReturnCode::Success.getValue();
        }
    }

    if (curr_state_status != MOM_STATE_COMPLETE) {
        if (ticket.curr_state > MOMDB_BLOCK_STATE) {
            rcode = processTktFlow(ticket);
        }
    }

    return rcode;
}

int momsrvr_business::processReporting(momtkt& ticket)
{
    const char* GUTS_VALUE_FUNCTION_NAME = "processReporting";
    GUTZ_METRICS_TAGGED_DISTRIBUTION_TIMER_MS(
        GUTS_NAMESPACE_ROOT.c_str(), GUTS_METRIC_MOMSRVR_FUNCTION.c_str(),
        GUTS_TAG_FUNCTION_NAME.c_str(), GUTS_VALUE_FUNCTION_NAME);
    aim_reporting_util reportObj;
    reportObj.processReporting(ticket);
    return 0;
}

FlowReturnCode momsrvr_business::getFirmFlow(aimcommon::OutParam<momtkt> ticket, bool isStartState)
{
    using namespace aimcommon;
    using namespace flowbotclient;

    BAEL_LOG_SET_CATEGORY(__func__);

    FlowReturnCode rcode = FlowReturnCode::Error;

    if (bbit_momsrvr_using_flowbot__value()) {
        // Need to set ticket.curr_state and ticket.next_state correctly
        Step currentStep;

        const momtkt& tkt = getOutParam(ticket);
        bdlt::DateTz dateCreated;
        int rc = getUtils().convertDateFromIntToDateTz(tkt.trade_date, dateCreated);

        if (rc == 0) {
            {
                GUTZ_METRICS_TAGGED_DISTRIBUTION_TIMER_MS(
                    GUTS_NAMESPACE_ROOT.c_str(), GUTS_METRIC_MOMSRVR_FLOWBOT.c_str(),
                    GUTS_TAG_FUNCTION_NAME.c_str(), "getCurrentFlowStep");
                rcode = convertToFlowReturnCode(d_flowbotClient->getCurrentFlowStep(
                    PricingNumber(getOutParam(ticket).prcnum), dateCreated,
                    TicketNumber(getOutParam(ticket).tktnum), makeOutParam(currentStep)));
            }
            if (FlowReturnCode::Success == rcode) {
                setOutParam(ticket).curr_state
                    = convertFlowbotActionToMomState(currentStep.action());
                setOutParam(ticket).next_state
                    = convertFlowbotActionToMomState(currentStep.action());

                const momtkt& setTicket = getOutParam(ticket);
                BAEL_LOG_INFO << __func__ << ": Successful return from flowbot for "
                              << " prcnum=" << setTicket.prcnum << " tktnum=" << setTicket.tktnum
                              << " current state=" << setTicket.curr_state
                              << " next state=" << setTicket.next_state << BAEL_LOG_END;

            } else if (FlowReturnCode::FlowEnd == rcode) {
                BAEL_LOG_INFO << __func__ << ": End of flow returned from flowbot. "
                              << BAEL_LOG_END;
            } else {
                BAEL_LOG_ERROR << __func__ << ": Failed to get flow flags from flowbot. "
                               << " error_type=get_flowflags_from_flowbot_failed"
                               << " rcode=" << rcode.getValue() << BAEL_LOG_END;
            }
        } else {
            BAEL_LOG_ERROR << __func__ << "Failed to convert trade date."
                           << " error_type=convert_trade_date_failed"
                           << " Trade date is: " << tkt.trade_date << " prcnum=" << tkt.prcnum
                           << " tktnum=" << tkt.tktnum << " rcode=" << rc << BAEL_LOG_END;
        }
    } else {
        {
            GUTZ_METRICS_TAGGED_DISTRIBUTION_TIMER_MS(
                GUTS_NAMESPACE_ROOT.c_str(), GUTS_METRIC_MOMSRVR_DBQUERY.c_str(),
                GUTS_TAG_FUNCTION_NAME.c_str(), "updateTktflow");
            rcode = convertToFlowReturnCode(
                d_ticketInterface->getFirmflow(getOutParam(ticket), &setOutParam(ticket).curr_state,
                                               &setOutParam(ticket).next_state, isStartState));
        }

        if (FlowReturnCode::Success != rcode && FlowReturnCode::FlowEnd != rcode
            && FlowReturnCode::NoStep != rcode) {
            const momtkt& resultTicket = getOutParam(ticket);
            BAEL_LOG_ERROR << __func__ << ": getFirmflow failed."
                           << " prcnum=" << resultTicket.prcnum << " tktnum=" << resultTicket.tktnum
                           << " product=" << resultTicket.product << " is_start=" << isStartState
                           << " curr_state=" << resultTicket.curr_state
                           << " next_state=" << resultTicket.next_state
                           << " rcode=" << rcode.getValue() << " error_type=getFirmFlow_failed"
                           << BAEL_LOG_END;
        }
    }

    return rcode;
}

FlowReturnCode momsrvr_business::moveToNextFlowStep(aimcommon::OutParam<momtkt> ticket,
                                                    int currentStepStatus)
{
    using namespace aimcommon;
    using namespace flowbotclient;

    BAEL_LOG_SET_CATEGORY(__func__);

    FlowReturnCode rcode = FlowReturnCode::Success;
    if (bbit_momsrvr_using_flowbot__value()) {
        Step currentStep;

        const momtkt& tkt = getOutParam(ticket);
        bdlt::DateTz dateCreated;
        int rc = getUtils().convertDateFromIntToDateTz(tkt.trade_date, dateCreated);
        if (rc == 0) {
            GUTZ_METRICS_TAGGED_DISTRIBUTION_TIMER_MS(
                GUTS_NAMESPACE_ROOT.c_str(), GUTS_METRIC_MOMSRVR_FLOWBOT.c_str(),
                GUTS_TAG_FUNCTION_NAME.c_str(), "moveToNextFlowStep");
            rcode = convertToFlowReturnCode(d_flowbotClient->moveToNextFlowStep(
                PricingNumber(tkt.prcnum), dateCreated, TicketNumber(tkt.tktnum),
                makeOutParam(currentStep)));
        } else {
            BAEL_LOG_ERROR << __func__ << ": Failed to convert trade date."
                           << " error_type=convert_trade_date_failed"
                           << " trade_date=" << tkt.trade_date << " prcnum=" << tkt.prcnum
                           << " tktnum=" << tkt.tktnum << " rcode=" << rc << BAEL_LOG_END;
        }
    } else if (getOutParam(ticket).tktaction != UPD_TKT) {
        int original_curr_state = getOutParam(ticket).curr_state;

        {
            GUTZ_METRICS_TAGGED_DISTRIBUTION_TIMER_MS(
                GUTS_NAMESPACE_ROOT.c_str(), GUTS_METRIC_MOMSRVR_DBQUERY.c_str(),
                GUTS_TAG_FUNCTION_NAME.c_str(), "updateTktflow");
            rcode = convertToFlowReturnCode(d_ticketInterface->updateTktflow(
                setOutParam(ticket), getOutParam(ticket).curr_state, getOutParam(ticket).next_state,
                getOutParam(ticket).facility, currentStepStatus));
        }

        setOutParam(ticket).curr_state = original_curr_state;

        if (FlowReturnCode::Error == rcode) {
            BAEL_LOG_ERROR << __func__ << ": updateTktflow failed."
                           << " tktnum=" << getOutParam(ticket).tktnum
                           << " prcnum=" << getOutParam(ticket).prcnum
                           << " rcode=" << rcode.getValue() << " error_type=updateTktflow_failed "
                           << BAEL_LOG_END;
        }
    }

    return rcode;
}

FlowReturnCode momsrvr_business::getFlowFlags(const momtkt& ticket,
                                              aimcommon::OutParam<bool> autoRelease,
                                              aimcommon::OutParam<bool> waitForStatus)
{
    using namespace aimcommon;
    using namespace flowbotclient;

    BAEL_LOG_SET_CATEGORY(__func__);

    FlowReturnCode rcode = FlowReturnCode::Error;

    if (bbit_momsrvr_using_flowbot__value()) {
        Step currentStep;

        bdlt::DateTz dateCreated;
        int rc = getUtils().convertDateFromIntToDateTz(ticket.trade_date, dateCreated);

        if (rc == 0) {
            {
                GUTZ_METRICS_TAGGED_DISTRIBUTION_TIMER_MS(
                    GUTS_NAMESPACE_ROOT.c_str(), GUTS_METRIC_MOMSRVR_FLOWBOT.c_str(),
                    GUTS_TAG_FUNCTION_NAME.c_str(), "getCurrentFlowStep");
                rcode = convertToFlowReturnCode(d_flowbotClient->getCurrentFlowStep(
                    PricingNumber(ticket.prcnum), dateCreated, TicketNumber(ticket.tktnum),
                    makeOutParam(currentStep)));
            }
            if (FlowReturnCode::Success == rcode) {
                setOutParam(autoRelease) = (currentStep.autoRelease());
                setOutParam(waitForStatus) = (currentStep.waitForStatus());
            } else if (FlowReturnCode::FlowEnd == rcode) {
                BAEL_LOG_INFO << __func__ << ": End of flow returned from flowbot. "
                              << BAEL_LOG_END;
            } else {
                BAEL_LOG_ERROR << __func__ << ": Failed to get flow flags from flowbot."
                               << " tktnum=" << ticket.tktnum << " prcnum=" << ticket.prcnum
                               << " rcode=" << rcode.getValue()
                               << " error_type=get_flowflags_from_flowbot_failed" << BAEL_LOG_END;
            }
        } else {
            BAEL_LOG_ERROR << __func__ << "Failed to convert trade date."
                           << " error_type=convert_trade_date_failed"
                           << " Trade date is: " << ticket.trade_date << " prcnum=" << ticket.prcnum
                           << " tktnum=" << ticket.tktnum << " rcode=" << rc << BAEL_LOG_END;
        }
    } else {
        int autoReleaseEnabled = 0;
        int waitForStatusEnabled = 0;

        {
            GUTZ_METRICS_TAGGED_DISTRIBUTION_TIMER_MS(
                GUTS_NAMESPACE_ROOT.c_str(), GUTS_METRIC_MOMSRVR_DBQUERY.c_str(),
                GUTS_TAG_FUNCTION_NAME.c_str(), "getFlowflags");
            rcode = convertToFlowReturnCode(d_ticketInterface->getFlowflags(
                ticket, &autoReleaseEnabled, &waitForStatusEnabled, IsFlowStart::False));
        }
        if (FlowReturnCode::Success == rcode || FlowReturnCode::FlowEnd == rcode) {
            setOutParam(autoRelease) = (1 == autoReleaseEnabled);
            setOutParam(waitForStatus) = (1 == waitForStatusEnabled);
            rcode = FlowReturnCode::Success;
        } else {
            BAEL_LOG_ERROR << __func__ << ": Failed to get flow flags."
                           << " tktnum=" << ticket.tktnum << " prcnum=" << ticket.prcnum
                           << " rcode=" << rcode.getValue() << " error_type=getFlowFlags_failed"
                           << BAEL_LOG_END;
        }
    }

    return rcode;
}

FlowReturnCode momsrvr_business::convertToFlowReturnCode(int rcode)
{
    switch (rcode) {
    case (0):
        return FlowReturnCode::Success;
    case (1):
        return FlowReturnCode::Error;
    case (2):
        return FlowReturnCode::NoStep;
    case (99):
        return FlowReturnCode::FlowEnd;
    default:
        return FlowReturnCode::Error;
    }
}

FlowReturnCode momsrvr_business::convertToFlowReturnCode(flowbotclient::ReturnCode rcode)
{
    if (flowbotclient::ReturnCode::Success == rcode) {
        return FlowReturnCode::Success;
    } else if (flowbotclient::ReturnCode::FlowDone == rcode) {
        return FlowReturnCode::FlowEnd;
    } else if (flowbotclient::ReturnCode::NoStep == rcode) {
        return FlowReturnCode::NoStep;
    } else {
        return FlowReturnCode::Error;
    }
}

int momsrvr_business::convertFlowbotActionToMomState(flowbotclient::Action::Value action)
{
    bsl::string actionStr(flowbotclient::Action::toString(action));

    if (actionStr == "BLOCK") {
        return MOMDB_BLOCK_STATE;
    } else if (actionStr == "ALLOCATION") {
        return MOMDB_ALLOCATIONS_STATE;
    } else if (actionStr == "TRADE_FEED") {
        return MOMDB_TRADEFEED_STATE;
    } else if (actionStr == "SETTLEMENT") {
        return MOMDB_SETTLEMENT_STATE;
    } else {
        return -1;
    }
}

FlowReturnCode momsrvr_business::getCurrentFlowStep(momtkt& ticket, bool isStartState)
{
    using namespace aimcommon;
    BAEL_LOG_SET_CATEGORY(__func__);

    int curr_state_status = 0;
    FlowReturnCode rcode;

    if (!isStartState && !bbit_momsrvr_using_flowbot__value()) {
        GUTZ_METRICS_TAGGED_DISTRIBUTION_TIMER_MS(GUTS_NAMESPACE_ROOT.c_str(),
                                                  GUTS_METRIC_MOMSRVR_DBQUERY.c_str(),
                                                  GUTS_TAG_FUNCTION_NAME.c_str(), "getTktStatus");
        int rc = d_ticketInterface->getTktStatus(ticket, ticket.curr_state, curr_state_status);
        if (rc) {
            BAEL_LOG_ERROR << __func__ << ": getTktStatus failed."
                           << " rcode=" << rc << " tktnum=" << ticket.tktnum
                           << " prcnum=" << ticket.prcnum
                           << " db_error=" << d_ticketInterface->getLastTranErrorStr()
                           << " error_type=getTktStatus_failed" << BAEL_LOG_END;
            return FlowReturnCode::Error;
        }
    }

    rcode = getFirmFlow(makeOutParam(ticket), isStartState);
    if (rcode == FlowReturnCode::NoStep) {
        BAEL_LOG_INFO << __func__ << ": no workflow matched for tktnum=" << ticket.tktnum
                      << " prcnum=" << ticket.prcnum << BAEL_LOG_END;
    } else if (rcode != FlowReturnCode::Success && rcode != FlowReturnCode::FlowEnd) {
        BAEL_LOG_ERROR << __func__ << ": getFirmFlow failed."
                       << " rcode=" << rcode.getValue() << " prcnum=" << ticket.prcnum
                       << " tktnum=" << ticket.tktnum
                       << " db_error=" << d_ticketInterface->getLastTranErrorStr()
                       << " error_type=getFirmFlow_failed" << BAEL_LOG_END;
    }
    return rcode;
}

bool momsrvr_business::getCurrentTktStepStatus(const momtkt& ticket,
                                               aimcommon::OutParam<int> stepStatus)
{
    using namespace aimcommon;
    BAEL_LOG_SET_CATEGORY("M_MOMSRVR.MOMSRVR_BUSINESS.GETCURRENTTKTSTEPSTATUS");

    switch (ticket.curr_state) {
    case MOMDB_BLOCK_STATE:
        setOutParam(stepStatus) = ticket.blockstatus;
        break;
    case MOMDB_ALLOCATIONS_STATE:
        setOutParam(stepStatus) = ticket.allocstatus;
        break;
    case MOMDB_TRADEFEED_STATE:
        setOutParam(stepStatus) = ticket.tradfeedstatus;
        break;
    case MOMDB_SETTLEMENT_STATE:
        setOutParam(stepStatus) = ticket.settlementstatus;
        break;
    default:
        BAEL_LOG_ERROR << "Unrecognized ticket curr_state"
                       << " tktnum=" << ticket.tktnum << " prcnum=" << ticket.prcnum
                       << " curr_state=" << ticket.curr_state
                       << " error_type=ticket_unrecognized_curr_state" << BAEL_LOG_END;
        return false;
    }
    return true;
}

} // namespace momsrvr
} // namespace BloombergLP
